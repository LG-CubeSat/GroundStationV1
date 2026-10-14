#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "ccsds/ccsds.h"
#include "radio/radio.h"

#define MAX_PACKET_SIZE 256U // largest packet we expect, placeholder for now

typedef enum : uint8_t {
    COMMAND_0 = 0x00,
    COMMAND_1 = 0x01,
    COMMAND_2 = 0x02,
} command_t;

ccsds_status_t telemetry_handler(const uint8_t *packet, size_t packet_length, void *user_data)
{
    // handle raw telemetry packet
    return CCSDS_OK;
}

ccsds_status_t handler(const uint8_t *packet, size_t packet_length, void *user_data) // ingest.c in FlightSoftwareV1.1 has a lot of useful information
{
    ccsds_status_t parse_status;
    lg_ccsds_profile_view_t *view = user_data;
    printf("Debug: %zu\n", packet_length);
    parse_status = lg_ccsds_profile_parse(packet, packet_length, view);
    if (parse_status != CCSDS_OK) {
        fprintf(stderr, "CCSDS packet invalid: %s\n", ccsds_status_string(parse_status));
        return CCSDS_OK;
    }
    switch (view->metadata.content_type) {
        case LG_CCSDS_CONTENT_RAW_TELEMETRY:
            return telemetry_handler(view->payload, view->payload_length, user_data);
        case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY:
            fprintf(stderr, "Recieved compressed telemetry data, decompressing...\n");
            uint8_t decoded_samples[1024]; // placeholder
            size_t decoded_length;
            ccsds_status_t decode_status = ccsds_121_decode(&view->metadata.compression,
                                                            view->payload, view->payload_length,
                                                            view->metadata.item_count,
                                                            decoded_samples, sizeof decoded_samples, &decoded_length);
            if (decode_status != CCSDS_OK) {
                fprintf(stderr, "Failed to decompress CCSDS packet: %s\n", ccsds_status_string(decode_status));
                return CCSDS_OK;
            }
            return telemetry_handler(decoded_samples, decoded_length, user_data); // assumes that decoded_samples will end up in the same format as the packet
        case LG_CCSDS_CONTENT_SSDV:
            fprintf(stderr, "Recieved SSDV encoded image data\n");
            // send to ssdv handler
            break;
        case LG_CCSDS_CONTENT_COMMAND:
            fprintf(stderr, "Recieved command: 0x%02X\n", *(view->payload));
            // what kind of commands are we sending from the satellite? ik we are sending commands to sat but are there any from sat?
            break;
        default:
            fprintf(stderr, "Unknown content type of CCSDS packet: %d\n", (int)view->metadata.content_type);
            return CCSDS_OK;
    }
    return CCSDS_OK;
}

int main(void)
{
    uint8_t radio_buffer[MAX_PACKET_SIZE];
    size_t radio_length;

    uint8_t payload[MAX_PACKET_SIZE];
    lg_ccsds_content_type_t content_type;
    size_t payload_length;
    uint32_t item_count;
    uint16_t sequence_count = 0;
    uint8_t wrapped[MAX_PACKET_SIZE];
    size_t wrapped_length;

    uint8_t incoming_buffer[MAX_PACKET_SIZE];
    size_t byte_count;
    ccsds_stream_parser_t receiver;
    uint8_t receive_storage[MAX_PACKET_SIZE];
    lg_ccsds_profile_view_t view;
    size_t packets_delivered = 0;

    ccsds_status_t init_status = ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage); // starts parser
    if (init_status != CCSDS_OK) {
        fprintf(stderr, "CCSDS parser initiation failure: %s\n", ccsds_status_string(init_status));
        return EXIT_FAILURE;
    }

    for (;;) {
        // --- SENDING CCSDS ----

        // example packet
        printf("Enter command byte (hex): ");
        int input_status = scanf("%hhx", &payload[0]); // human-input placeholder
        if (input_status != 1) {
            fprintf(stderr, "Failed to read command byte\n");
            return EXIT_FAILURE;
        }
        payload_length = 1;
        // payload_length = read_command(&payload); // or something similar for actual
        content_type = LG_CCSDS_CONTENT_COMMAND; // should always be command at ground
        item_count = 1; // amount of commands in payload

        ccsds_status_t encode_status = lg_ccsds_profile_build(content_type,
                                                            sequence_count, item_count, NULL,
                                                            payload, payload_length,
                                                            wrapped, sizeof wrapped, &wrapped_length);
        if (encode_status != CCSDS_OK) {
            fprintf(stderr, "CCSDS encoder failure: %s\n", ccsds_status_string(encode_status));
            return EXIT_FAILURE;
        }
        sequence_count ++;
        if (sequence_count == 0x3FFF) {
            sequence_count = 0;
        }

        // mock radio send
        memcpy(radio_buffer, wrapped, wrapped_length);
        radio_length = wrapped_length;
        // radio_send(radio_buffer, radio_length);
        printf("Encoded bytes: ");
        for (size_t i = 0; i < radio_length; i++) {
            printf("%02X ", radio_buffer[i]);
        }
        printf("\n");

        // --- RECEIVING CCSDS ---

        // mock radio recieve
        memcpy(incoming_buffer, radio_buffer, radio_length);
        byte_count = radio_length;
        // byte_count = radio_receive(radio_buffer, (uint16_t)sizeof radio_buffer);

        ccsds_status_t feed_status = ccsds_stream_parser_feed(&receiver, incoming_buffer, byte_count,
                                                              handler, &view, &packets_delivered);
        if (feed_status != CCSDS_OK) { // data -> packets, handled in callback handler()
            fprintf(stderr, "CCSDS parser failure: %s\n", ccsds_status_string(feed_status));
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
