#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "radio.h"

/*
 * Local radio simulator (POSIX): one UDP datagram contains one CCSDS packet.
 * GSTest (src/test.c) -> 127.0.0.1:9000 -> radio_receive() -> GSMain
 * GSMain -> radio_send() -> 127.0.0.1:9001 -> GSTest
 * IPv4 UDP limits a complete packet to 65507 bytes (65501 payload bytes).
 */
#define RADIO_RECEIVE_PORT 9000
#define RADIO_TRANSMIT_PORT 9001
#define RADIO_DATAGRAM_CAPACITY 65507U

static int radio_socket = -1;

static void close_radio(void) {
    if (radio_socket >= 0) {
        (void)close(radio_socket);
        radio_socket = -1;
    }
}

static int open_radio(void) {
    struct sockaddr_in address = {0};
    int fd;
    int flags;

    if (radio_socket >= 0) return 0;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[RADIO] socket");
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[RADIO] nonblocking socket");
        (void)close(fd);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(RADIO_RECEIVE_PORT);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[RADIO] bind 127.0.0.1:9000");
        (void)close(fd);
        return -1;
    }
    if (atexit(close_radio) != 0) {
        fprintf(stderr, "[RADIO] Unable to register socket cleanup\n");
        (void)close(fd);
        return -1;
    }
    radio_socket = fd;
    fprintf(stderr, "[RADIO] UDP receive: 127.0.0.1:%d; transmit: 127.0.0.1:%d\n",
            RADIO_RECEIVE_PORT, RADIO_TRANSMIT_PORT);
    return 0;
}

int radio_send(const uint8_t *data, size_t length) {
    struct sockaddr_in destination = {0};
    ssize_t sent;

    if (data == NULL || length == 0U || length > RADIO_DATAGRAM_CAPACITY) {
        fprintf(stderr, "[RADIO] Invalid UDP transmit buffer/length: %zu\n", length);
        return -1;
    }
    if (open_radio() != 0) return -1;
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    destination.sin_port = htons(RADIO_TRANSMIT_PORT);
    sent = sendto(radio_socket, data, length, 0,
                  (const struct sockaddr *)&destination, sizeof(destination));
    if (sent < 0) {
        perror("[RADIO] sendto");
        return -1;
    }
    if ((size_t)sent != length) return -1;
    return 0;
}

int radio_receive(uint8_t *buffer, uint16_t max_length) {
    struct iovec buffer_view = {.iov_base = buffer, .iov_len = max_length};
    struct msghdr message = {0};
    ssize_t received;

    if (buffer == NULL || max_length == 0U) return -1;
    if (open_radio() != 0) return -1;
    message.msg_iov = &buffer_view;
    message.msg_iovlen = 1;
    received = recvmsg(radio_socket, &message, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        perror("[RADIO] recvmsg");
        return -1;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        fprintf(stderr, "[RADIO] Dropping datagram larger than receive buffer\n");
        return -1;
    }
    return (int)received;
}
