#include "radio_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ccsds/profile.h"
#include "radio.h"

static int use_sim;
static int receive_fd = -1;
static int transmit_fd = -1;
static struct sockaddr_in flight_address;
static uint16_t command_sequence;

static int port_from_env(const char *name, int fallback) {
    const char *value = getenv(name);
    if (value == NULL || *value == '\0') return fallback;
    char *end = NULL;
    long port = strtol(value, &end, 10);
    return end == value || *end != '\0' || port < 1 || port > 65535 ? -1 : (int)port;
}

int gs_radio_init(void) {
    const char *sim = getenv("GS_SIM_RF");
    use_sim = sim != NULL && strcmp(sim, "1") == 0;
    if (!use_sim) return 0;

    int downlink = port_from_env("GS_SIM_DOWNLINK_PORT", 5600);
    int uplink = port_from_env("GS_SIM_UPLINK_PORT", 5601);
    if (downlink < 0 || uplink < 0) return -1;

    receive_fd = socket(AF_INET, SOCK_DGRAM, 0);
    transmit_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (receive_fd < 0 || transmit_fd < 0) goto error;

    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons((uint16_t)downlink);
    if (bind(receive_fd, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        fcntl(receive_fd, F_SETFL, O_NONBLOCK) < 0) goto error;

    memset(&flight_address, 0, sizeof(flight_address));
    flight_address.sin_family = AF_INET;
    flight_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    flight_address.sin_port = htons((uint16_t)uplink);
    fprintf(stderr, "GS simulator RF: UDP downlink %d, uplink %d\n", downlink, uplink);
    return 0;
error:
    perror("GS simulator RF init");
    gs_radio_close();
    return -1;
}

int gs_radio_receive_packet(uint8_t *buffer, uint16_t max_length) {
    if (!use_sim) return radio_receive_packet(buffer, max_length);
    if (buffer == NULL || max_length == 0) return -1;
    uint8_t packet[RADIO_MAX_PACKET_SIZE + 1];
    ssize_t received = recv(receive_fd, packet, sizeof(packet), 0);
    if (received < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    if (received > RADIO_MAX_PACKET_SIZE || received > max_length) return -1;
    memcpy(buffer, packet, (size_t)received);
    return (int)received;
}

int gs_radio_send_command(const uint8_t *payload, size_t length) {
    if (!use_sim) return radio_send_command(payload, length);
    if (payload == NULL || length == 0 || length > RADIO_MAX_PAYLOAD) return -1;
    uint8_t packet[RADIO_MAX_PACKET_SIZE];
    size_t packet_length = 0;
    if (lg_ccsds_profile_build(LG_CCSDS_CONTENT_COMMAND, command_sequence++, 1, NULL,
                               payload, length, packet, sizeof(packet), &packet_length) != CCSDS_OK)
        return -1;
    ssize_t sent = sendto(transmit_fd, packet, packet_length, 0,
                          (struct sockaddr *)&flight_address, sizeof(flight_address));
    return sent == (ssize_t)packet_length ? 0 : -1;
}

void gs_radio_close(void) {
    if (receive_fd >= 0) close(receive_fd);
    if (transmit_fd >= 0) close(transmit_fd);
    receive_fd = transmit_fd = -1;
}
