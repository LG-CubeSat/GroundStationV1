#include <stdio.h>
#include "ccsds/ccsds.h"

#define MAX_PACKET_SIZE 256U // largest packet we expect, placeholder for now

#define INPUT_PATH // put stuff here later
#define OUTPUT_PATH // put stuff here later

static size_t radio_read(uint8_t *buffer, size_t max_bytes)
{
    (void)buffer;
    (void)max_bytes;
    return 0;
} // slop stub

static ccsds_status_t callback(const uint8_t *packet, size_t packet_length, void *user_data)
{
    ccsds_status_t parse_status;
    lg_ccsds_profile_view_t *view = user_data;
    parse_status = lg_ccsds_profile_parse(packet, packet_length, view);
    if (parse_status != CCSDS_OK) {
        fprintf(stderr, "CCSDS packet invalid: %s\n", ccsds_status_string(parse_status));
        return CCSDS_OK; // not entirely sure if this is right but usage.md seems to think so
    }
    switch (view->metadata.content_type) {
        case LG_CCSDS_CONTENT_RAW_TELEMETRY:
            // do stuff? why would we be sending raw telemetry
            break;
        case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY:
            // use view.metadata.compression to decode but also why would we be sending this
            break;
        case LG_CCSDS_CONTENT_SSDV:
            // send to ssdv handler
            break;
        case LG_CCSDS_CONTENT_COMMAND:
            // what kind of commands are we sending from the satellite?
            break;
        default:
            fprintf(stderr, "Unknown content type of CCSDS packet: %d\n", (int)view->metadata.content_type);
            return CCSDS_OK; // also not completely sure on this return
    }
    return CCSDS_OK;
}

int main(void) {
    uint8_t radio_buffer[MAX_PACKET_SIZE];
    size_t byte_count;
    uint8_t receive_storage[MAX_PACKET_SIZE];
    size_t packets_delivered = 0;
    ccsds_stream_parser_t receiver;
    lg_ccsds_profile_view_t view; // finish setting this up

    // receiving stuff
    // run this code when information recieved by radio
    ccsds_status_t init_status = ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage); // packets -> storage to be read
    if (init_status != CCSDS_OK) {
    fprintf(stderr, "CCSDS parser initiation failure: %s\n", ccsds_status_string(init_status));
    return EXIT_FAILURE;
}    // does this function go first or second

    for (;;) {
        byte_count = radio_read(&radio_buffer, sizeof radio_buffer); // stub function reads from radio, returns # of bytes read, replace once we have/write radio code

        ccsds_status_t feed_status = ccsds_stream_parser_feed(&receiver, radio_buffer, byte_count,
                                                     callback, &view, &packets_delivered); // data -> packets, stats displayed in view
        if (feed_status != CCSDS_OK) {
            fprintf(stderr, "CCSDS parser failure: %s\n", ccsds_status_string(feed_status));
            return EXIT_FAILURE;
        }
        // if program is superloop: i need to redo this
        // if program is multithreaded: does this need to break out?
    }
    

    // sending stuff, wip
    /*
    lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY,
                         sequence_count, item_count, NULL,
                         telemetry, telemetry_length,
                         packet_buffer, sizeof packet_buffer, &packet_length); // none of these parameters are defined so far
    */
    return EXIT_SUCCESS;
}
