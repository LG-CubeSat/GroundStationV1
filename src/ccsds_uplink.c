#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "ccsds/ccsds.h"
#include "radio.h"

#define PACKET_SIZE 256U // placeholder

int main() {
    sequence_count = 0; // placeholder
        item_count = 1; // if sending one command
        uint8_t packet_buffer[PACKET_SIZE];
        size_t packet_length;
        for(;;) {
            uint8_t command[PACKET_SIZE]; // if we make custom command_t, will need decoder/encoder
            size_t command_length = 0; // placeholder
            lg_ccsds_profile_build(LG_CCSDS_CONTENT_COMMAND,
                                sequence_count, item_count, NULL,
                                command, command_length,
                                packet_buffer, sizeof packet_buffer, &packet_length);
            radio_send(packet_buffer, packet_length);
            // sequence_count = (sequence_count + 1) % CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT; // increments every time you send command, keep if this command stream is meant to be constantly sending and sat is checking continuity
        }
    return EXIT_SUCCESS;
}
