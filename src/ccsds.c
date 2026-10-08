#include <stdio.h>
#include "ccsds/ccsds.h"
#include "radio.h"

#define MAX_PACKET_SIZE 256U // largest packet we expect, placeholder for now

ccsds_status_t telemetry_handler(const uint8_t *packet, size_t packet_length, void *user_data)
{
    // handle raw telemetry packet
    return CCSDS_OK;
}

ccsds_status_t handler(const uint8_t *packet, size_t packet_length, void *user_data)
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
            return telemetry_handler(packet, packet_length, user_data);
        case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY:
            fprintf(stderr, "Recieved compressed telemetry data, decompressing...");
            uint8_t decoded_samples[1024]; // placeholder
            size_t decoded_length;
            ccsds_121_decode(&view.metadata.compression,
                             view.payload, view.payload_length,
                             view.metadata.item_count,
                             decoded_samples, sizeof decoded_samples, &decoded_length);
            return telemetry_handler(decoded_samples, decoded_length, user_data); // assumes that decoded_samples will end up in the same format as the packet
        case LG_CCSDS_CONTENT_SSDV:
            fprintf(stderr, "Recieved SSDV encoded image data");
            // send to ssdv handler
            break;
        case LG_CCSDS_CONTENT_COMMAND:
            fprintf(stderr, "Recieved command");
            // what kind of commands are we sending from the satellite? ik we are sending commands to sat but are there any from sat?
            break;
        default:
            fprintf(stderr, "Unknown content type of CCSDS packet: %d\n", (int)view->metadata.content_type);
            return CCSDS_OK; // also not completely sure on this return
    }
    return CCSDS_OK;
}

int main(void) {
    ccsds_stream_parser_t receiver;
    uint8_t receive_storage[MAX_PACKET_SIZE];
    lg_ccsds_profile_view_t view;
    uint8_t radio_buffer[MAX_PACKET_SIZE];
    size_t byte_count;
    size_t packets_delivered = 0;

    // receiving stuff
    // run this code when information recieved by radio
    ccsds_status_t init_status = ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage); // starts parser
    if (init_status != CCSDS_OK) {
    fprintf(stderr, "CCSDS parser initiation failure: %s\n", ccsds_status_string(init_status));
    return EXIT_FAILURE;
    }

    for (;;) {
        byte_count = radio_recv(&radio_buffer, sizeof radio_buffer); // stub, always reads 0. Radio needs to say how many bytes are read, impossible from this end because raw bytes mean there is no terminator character
        ccsds_status_t feed_status = ccsds_stream_parser_feed(&receiver, radio_buffer, byte_count,
                                                              handler, &view, &packets_delivered); // data -> packets, handled in callback handler()
        if (feed_status != CCSDS_OK) {
            fprintf(stderr, "CCSDS parser failure: %s\n", ccsds_status_string(feed_status));
            return EXIT_FAILURE;
        }
        // if program is superloop: put everything thats in the for loop into the superloop i guess
        // if program is multithreaded: does this need to break out? if not my work here is done
    }
    
    // sending stuff, wip
    /*
    ccsds_cmd_t command;
    lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY,
                         sequence_count, item_count, NULL,
                         telemetry, telemetry_length,
                         packet_buffer, sizeof packet_buffer, &packet_length); // none of these parameters are defined so far
    */
    return EXIT_SUCCESS;
}
