#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>

#include "ccsds/ccsds.h"
#include "groundstation_message.h"
#include "radio.h"

#define GROUNDSTATION_NNG_URL "ipc:///tmp/groundstation.ipc"
#define MAX_PACKET_SIZE 256U

int main(void)
{
    groundstation_packet_t payload;
    size_t payload_length;
    uint32_t item_count;
    uint16_t sequence_count = 0;
    uint8_t wrapped;
    size_t wrapped_length;

    int receiver_open = 0;
    int rv;

    nng_socket receiver = NNG_SOCKET_INITIALIZER;
    nng_msg *message = NULL;

    fprintf(stderr, "Opening NNG receiver socket...\n");
    if ((rv = nng_sub0_open(&receiver)) != 0) {
        fprintf(stderr, "Unable to open NNG receiver socket: %s\n", nng_strerror(rv));
        nng_close(receiver);
        return EXIT_FAILURE;
    }
    receiver_open = 1;
    fprintf(stderr, "NNG receiver socket open\n");
    if ((rv = nng_socket_set(receiver, NNG_OPT_SUB_SUBSCRIBE, "", 0)) != 0) {
        fprintf(stderr, "Unable to set subscriber settings %s: %s\n", GROUNDSTATION_NNG_URL, nng_strerror(rv));
        nng_close(receiver);
        return EXIT_FAILURE;
    }
    if ((rv = nng_dial(receiver, GROUNDSTATION_NNG_URL, NULL, 0)) != 0) {
        fprintf(stderr, "Unable to connect to %s: %s\n", GROUNDSTATION_NNG_URL, nng_strerror(rv));
        nng_close(receiver);
        return EXIT_FAILURE;
    }

    for (;;) {
        // receive from NNG
        if ((rv = nng_recvmsg(receiver, &message, 0)) != 0) {
            fprintf(stderr, "NNG receive error: %s\n", nng_strerror(rv));
            nng_msg_free(message);
            nng_close(receiver);
            return EXIT_FAILURE;
        }

        // decode NNG
        if (!groundstation_packet_deserialize(&payload, nng_msg_body(message), nng_msg_len(message))) {
            nng_msg_free(message);
            nng_close(receiver);
            fprintf(stderr, "Faliled to deserialize packet\n");
            return EXIT_FAILURE;
        }
        payload_length = payload.command.commands_length;
        item_count = payload.command.command_count; // amount of commands in payload

        // encode CCSDS
        ccsds_status_t encode_status = lg_ccsds_profile_build(LG_CCSDS_CONTENT_COMMAND,
                                                              sequence_count, item_count, NULL,
                                                              payload.command.commands, payload_length,
                                                              &wrapped, sizeof wrapped, &wrapped_length);
        if (encode_status != CCSDS_OK) {
            fprintf(stderr, "CCSDS encoder failure: %s\n", ccsds_status_string(encode_status));
            nng_msg_free(message);
            nng_close(receiver);
            return EXIT_FAILURE;
        }
        sequence_count ++;
        if (sequence_count == 0x3FFF) {
            sequence_count = 0;
        }

        // send to radio
        radio_send(&wrapped, wrapped_length);

        // reset
        nng_msg_free(message);
    }
    nng_close(receiver);
}