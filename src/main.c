#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>

#include "ccsds/ccsds.h"
#include "groundstation_message.h"
#include "radio.h"

#define GROUNDSTATION_NNG_URL "ipc:///tmp/groundstation.ipc"
#define RADIO_BUFFER_SIZE UINT16_MAX

typedef struct {
    nng_socket publisher;
} receive_context_t;

static volatile sig_atomic_t keep_running = 1;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static ccsds_status_t publish_packet(nng_socket publisher, const groundstation_packet_t *packet) {
    nng_msg *message;
    size_t packet_size;
    size_t written;
    int rv;

    if (!groundstation_packet_valid(packet)) {
        return CCSDS_ERROR_RANGE;
    }
    packet_size = groundstation_packet_size(packet->telemetry.content_type);
    rv = nng_msg_alloc(&message, packet_size);
    if (rv != 0) {
        fprintf(stderr, "NNG message allocation failed: %s\n", nng_strerror(rv));
        return CCSDS_ERROR_CALLBACK;
    }

    if (!groundstation_packet_serialize(packet, nng_msg_body(message), packet_size, &written)) {
        nng_msg_free(message);
        return CCSDS_ERROR_RANGE;
    }

    rv = nng_sendmsg(publisher, message, 0);
    if (rv != 0) {
        fprintf(stderr, "NNG publish failed: %s\n", nng_strerror(rv));
        nng_msg_free(message);
        return CCSDS_ERROR_CALLBACK;
    }
    return CCSDS_OK;
}

static ccsds_status_t forward_space_packet(const uint8_t *packet, size_t packet_length, void *user_context) {
    receive_context_t *context = user_context;
    lg_ccsds_profile_view_t view;
    groundstation_packet_t outgoing = {0};
    ccsds_status_t status;

    status = lg_ccsds_profile_parse(packet, packet_length, &view);
    if (status != CCSDS_OK) {
        fprintf(stderr, "Dropping invalid CCSDS profile: %s\n", ccsds_status_string(status));
        return CCSDS_OK;
    }

    switch (view.metadata.content_type) {
    case LG_CCSDS_CONTENT_RAW_TELEMETRY:
        if (view.payload_length > sizeof(outgoing.telemetry.samples)) {
            fprintf(stderr, "Telemetry exceeds shared packet capacity\n");
            return CCSDS_OK;
        }
        outgoing.telemetry.content_type = GROUNDSTATION_PACKET_TELEMETRY;
        outgoing.telemetry.sample_count = view.metadata.item_count;
        outgoing.telemetry.samples_length = (uint32_t)view.payload_length;
        memcpy(outgoing.telemetry.samples, view.payload, view.payload_length);
        break;

    case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY: {
        size_t bytes_per_sample;
        size_t decoded_capacity;
        size_t decoded_length = 0U;

        status = ccsds_121_bytes_per_sample(&view.metadata.compression, &bytes_per_sample);
        if (status != CCSDS_OK || bytes_per_sample == 0U ||
            view.metadata.item_count == 0U ||
            view.metadata.item_count > sizeof(outgoing.telemetry.samples) / bytes_per_sample) {
            fprintf(stderr, "Invalid telemetry sample format or sample count exceeds shared packet capacity\n");
            return CCSDS_OK;
        }

        decoded_capacity = (size_t)view.metadata.item_count * bytes_per_sample;
        status = ccsds_121_decode(&view.metadata.compression, view.payload, view.payload_length, view.metadata.item_count, outgoing.telemetry.samples, decoded_capacity, &decoded_length);
        if (status != CCSDS_OK) {
            fprintf(stderr, "Unable to decompress CCSDS telemetry: %s\n", ccsds_status_string(status));
            return CCSDS_OK;
        }

        outgoing.telemetry.content_type = GROUNDSTATION_PACKET_TELEMETRY;
        outgoing.telemetry.sample_count = view.metadata.item_count;
        outgoing.telemetry.samples_length = (uint32_t)decoded_length;
        outgoing.telemetry.bits_per_sample = view.metadata.compression.bits_per_sample;
        outgoing.telemetry.signed_samples = view.metadata.compression.signed_samples;
        break;
    }

    case LG_CCSDS_CONTENT_SSDV:
        if (view.payload_length != sizeof(outgoing.ssdv.data)) {
            fprintf(stderr, "Invalid SSDV packet length\n");
            return CCSDS_OK;
        }
        outgoing.ssdv.content_type = GROUNDSTATION_PACKET_SSDV;
        memcpy(outgoing.ssdv.data, view.payload, sizeof(outgoing.ssdv.data));
        break;

    case LG_CCSDS_CONTENT_COMMAND:
        if (view.payload_length > sizeof(outgoing.command.commands)) {
            fprintf(stderr, "Commands exceed shared packet capacity\n");
            return CCSDS_OK;
        }
        outgoing.command.content_type = GROUNDSTATION_PACKET_COMMAND;
        outgoing.command.command_count = view.metadata.item_count;
        outgoing.command.commands_length = (uint32_t)view.payload_length;
        memcpy(outgoing.command.commands, view.payload, view.payload_length);
        break;

    default:
        fprintf(stderr, "Unsupported CCSDS content type\n");
        return CCSDS_OK;
    }

    return publish_packet(context->publisher, &outgoing);
}

int main(void) {
    uint8_t radio_buffer[RADIO_BUFFER_SIZE];
    uint8_t packet_storage[CCSDS_SPACE_PACKET_MAX_TOTAL_SIZE];
    ccsds_stream_parser_t parser;
    receive_context_t context;
    nng_socket publisher = NNG_SOCKET_INITIALIZER;
    int rv;
    ccsds_status_t status;

    rv = nng_pub0_open(&publisher);
    if (rv != 0) {
        fprintf(stderr, "Unable to open NNG publisher: %s\n", nng_strerror(rv));
        return EXIT_FAILURE;
    }

    rv = nng_listen(publisher, GROUNDSTATION_NNG_URL, NULL, 0);
    if (rv != 0) {
        fprintf(stderr, "Unable to listen on %s: %s\n", GROUNDSTATION_NNG_URL, nng_strerror(rv));
        (void)nng_close(publisher);
        return EXIT_FAILURE;
    }

    status = ccsds_stream_parser_init(&parser, packet_storage, sizeof(packet_storage));
    if (status != CCSDS_OK) {
        fprintf(stderr, "Unable to initialize CCSDS parser: %s\n", ccsds_status_string(status));
        (void)nng_close(publisher);
        return EXIT_FAILURE;
    }

    context.publisher = publisher;
    /* Graceful shutdown: SIGINT or SIGTERM stops the loop so the publisher can close cleanly; this may be removed later if it is not needed. */
    (void)signal(SIGINT, request_shutdown);
    (void)signal(SIGTERM, request_shutdown);
    fprintf(stderr, "Receiving radio data; publishing CCSDS payloads on %s\n", GROUNDSTATION_NNG_URL);

    while (keep_running) {
        int received = radio_receive(radio_buffer, (uint16_t)sizeof(radio_buffer));
        size_t packets_delivered = 0U;

        if (received < 0) {
            fprintf(stderr, "Radio receive failed (%d); retrying\n", received);
            nng_msleep(1000);
            continue;
        }
        if (received == 0) {
            nng_msleep(10);
            continue;
        }
        if ((size_t)received > sizeof(radio_buffer)) {
            fprintf(stderr, "Radio returned an invalid byte count (%d)\n", received);
            continue;
        }

        status = ccsds_stream_parser_feed(&parser, radio_buffer, (size_t)received, forward_space_packet, &context, &packets_delivered);
        if (status != CCSDS_OK) {
            fprintf(stderr, "CCSDS stream parse failed: %s\n", ccsds_status_string(status));
            ccsds_stream_parser_reset(&parser);
        }
    }

    (void)nng_close(publisher);
    return EXIT_SUCCESS;
}
