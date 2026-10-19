#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>

#include "ccsds/ccsds.h"
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

/* The first message byte is the content type and serves as the topic prefix, so subscribers can filter by type; an empty subscription receives every payload. */
static ccsds_status_t publish_payload(nng_socket publisher, uint8_t content_type, const uint8_t *payload, size_t payload_length) {
    nng_msg *message;
    uint8_t *body;
    int rv;

    if (payload_length == SIZE_MAX) {
        return CCSDS_ERROR_RANGE;
    }
    rv = nng_msg_alloc(&message, payload_length + 1U);
    if (rv != 0) {
        fprintf(stderr, "NNG message allocation failed: %s\n", nng_strerror(rv));
        return CCSDS_ERROR_CALLBACK;
    }

    body = nng_msg_body(message);
    body[0] = content_type;
    if (payload_length != 0U) {
        memcpy(body + 1U, payload, payload_length);
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
    const uint8_t *payload;
    size_t payload_length;
    uint8_t content_type;
    uint8_t *decoded_payload = NULL;
    ccsds_status_t status;

    status = lg_ccsds_profile_parse(packet, packet_length, &view);
    if (status != CCSDS_OK) {
        fprintf(stderr, "Dropping invalid CCSDS profile: %s\n", ccsds_status_string(status));
        return CCSDS_OK;
    }

    payload = view.payload;
    payload_length = view.payload_length;
    content_type = (uint8_t)view.metadata.content_type;

    if (view.metadata.content_type == LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY) {
        size_t bytes_per_sample;
        size_t decoded_capacity;
        size_t decoded_length = 0U;

        status = ccsds_121_bytes_per_sample(&view.metadata.compression, &bytes_per_sample);
        if (status != CCSDS_OK || bytes_per_sample == 0U ||
            view.metadata.item_count > SIZE_MAX / bytes_per_sample) {
            fprintf(stderr, "Invalid CCSDS telemetry sample count or format\n");
            return CCSDS_OK;
        }

        decoded_capacity = (size_t)view.metadata.item_count * bytes_per_sample;
        decoded_payload = malloc(decoded_capacity);
        if (decoded_payload == NULL) {
            fprintf(stderr, "Unable to allocate decompressed telemetry buffer\n");
            return CCSDS_OK;
        }

        status = ccsds_121_decode(&view.metadata.compression, view.payload, view.payload_length, view.metadata.item_count, decoded_payload, decoded_capacity, &decoded_length);
        if (status != CCSDS_OK) {
            fprintf(stderr, "Unable to decompress CCSDS telemetry: %s\n", ccsds_status_string(status));
            free(decoded_payload);
            return CCSDS_OK;
        }

        payload = decoded_payload;
        payload_length = decoded_length;
        content_type = (uint8_t)LG_CCSDS_CONTENT_RAW_TELEMETRY;
    }

    status = publish_payload(context->publisher, content_type, payload, payload_length);
    free(decoded_payload);
    return status;
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
