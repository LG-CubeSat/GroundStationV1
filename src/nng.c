#include <stdio.h>
#include <stdlib.h>

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>

#define NNG_EXAMPLE_ENABLE_SEND 1
#define NNG_EXAMPLE_ENABLE_RECEIVE 1

static int report_failure(int rv, nng_socket sender, int sender_open, nng_socket receiver, int receiver_open) {
    fprintf(stderr, "NNG error: %s\n", nng_strerror(rv));
    if (sender_open) {
        (void)nng_close(sender);
    }
    if (receiver_open) {
        (void)nng_close(receiver);
    }
    return EXIT_FAILURE;
}

int main(void) {
    const char *url = "ipc:///tmp/groundstation-nng.ipc";
    nng_socket receiver = NNG_SOCKET_INITIALIZER;
    nng_socket sender = NNG_SOCKET_INITIALIZER;
    char payload[] = "Hello over local IPC";
    char received[sizeof(payload)] = {0};
    size_t received_size = sizeof(received);
    int receiver_open = 0;
    int sender_open = 0;
    int rv;

    if ((rv = nng_pair0_open(&receiver)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }
    receiver_open = 1;

    if ((rv = nng_pair0_open(&sender)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }
    sender_open = 1;

    /* Queue one message so the send can complete before this thread receives. */
    if ((rv = nng_socket_set_int(receiver, NNG_OPT_RECVBUF, 1)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }
    if ((rv = nng_listen(receiver, url, NULL, 0)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }
    if ((rv = nng_dial(sender, url, NULL, 0)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }

#if NNG_EXAMPLE_ENABLE_SEND
    if ((rv = nng_send(sender, payload, sizeof(payload) - 1, 0)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }
#endif

#if NNG_EXAMPLE_ENABLE_RECEIVE
    if ((rv = nng_recv(receiver, received, &received_size, 0)) != 0) {
        return report_failure(rv, sender, sender_open, receiver, receiver_open);
    }

    printf("Received over IPC: %.*s\n", (int)received_size, received);
#endif

    (void)nng_close(sender);
    (void)nng_close(receiver);
    return EXIT_SUCCESS;
}
