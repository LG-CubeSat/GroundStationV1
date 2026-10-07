#include <stdio.h>
#include "ccsds/ccsds.h"

#define MAX_PACKET_SIZE 256U // I don't know what the max packet size is, change later

#define INPUT_PATH // put stuff here later
#define OUTPUT_PATH // put stuff here later

int main(void) {
    // receiving stuff
    // run this code when information recieved by radio
    
    uint8_t radio_bytes = 1; // placeholder incoming information (data)
    uint8_t receive_storage[MAX_PACKET_SIZE];
    ccsds_stream_parser_t receiver;
    size_t packets_delivered = 0;

    lg_ccsds_profile_view_t view;
    ccsds_status_t status = ccsds_stream_parser_feed(&receiver, radio_bytes, sizeof radio_bytes,
                                                     g_ccsds_profile_parse, &view, &packets_delivered); // data -> packets, stats displayed in view
    if (status != CCSDS_OK) {
        perror() // insert something here
        return EXIT_FAILURE
    }

    ccsds_status_t status = ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage); // packets -> storage to be read
    if (status != CCSDS_OK) {
        perror() // insert something here
        return EXIT_FAILURE
    }

    // sending stuff, wip
    lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY,
                        sequence_count, item_count, NULL,
                        telemetry, telemetry_length,
                        packet_buffer, sizeof packet_buffer, &packet_length); // none of these parameters are defined so far
    return EXIT_SUCCESS;
}
