#include <stdio.h>
#include "ccsds/ccsds.h"

#define MAX_PACKET_SIZE 64 // I don't know what the max packet size is, change later

int main(void) {
    // receiving stuff
    // run this code when information recieved by radio

    // placeholder incoming information
    uint8_t radio_bytes = 1;

    uint8_t receive_storage[MAX_PACKET_SIZE];
    ccsds_stream_parser_t receiver;
    size_t packets_delivered = 0;

    lg_ccsds_profile_view_t view;

    ccsds_status_t status = ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage);
    if (status != CCSDS_OK) {
        printf("[PARSER] ERROR: %s" ccsds_status_string(status))
    }

    ccsds_status_t status = ccsds_stream_parser_feed(&receiver, radio_bytes, sizeof radio_bytes,
                                                    lg_ccsds_profile_parse, &view, &packets_delivered);
    if (status != CCSDS_OK) {
        printf("[PARSER] ERROR: %s" ccsds_status_string(status))
    }

    // sending stuff, wip
    lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY,
                        sequence_count, item_count, NULL,
                        telemetry, telemetry_length,
                        packet_buffer, sizeof packet_buffer, &packet_length);
    return 0;
}
