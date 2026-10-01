#ifndef CCSDS_STREAM_H
#define CCSDS_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "ccsds/status.h"

/*
 * A radio/UART driver may deliver a packet in many chunks or several packets
 * in one chunk. This parser uses caller-owned storage to recover complete
 * Space Packets without dynamic allocation.
 */
typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t buffered;
    size_t expected_packet_size;
} ccsds_stream_parser_t;

typedef ccsds_status_t (*ccsds_packet_callback_t)(
    const uint8_t *packet,
    size_t packet_length,
    void *user_context
);

ccsds_status_t ccsds_stream_parser_init(
    ccsds_stream_parser_t *parser,
    uint8_t *storage,
    size_t storage_capacity
);

void ccsds_stream_parser_reset(ccsds_stream_parser_t *parser);

ccsds_status_t ccsds_stream_parser_feed(
    ccsds_stream_parser_t *parser,
    const uint8_t *bytes,
    size_t byte_count,
    ccsds_packet_callback_t callback,
    void *user_context,
    size_t *packets_delivered
);

#endif
