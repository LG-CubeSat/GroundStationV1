#ifndef CCSDS_SPACE_PACKET_H
#define CCSDS_SPACE_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ccsds/status.h"

#define CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE 6U
#define CCSDS_SPACE_PACKET_MAX_DATA_SIZE 65536U
#define CCSDS_SPACE_PACKET_MAX_TOTAL_SIZE \
    (CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE + CCSDS_SPACE_PACKET_MAX_DATA_SIZE)
#define CCSDS_SPACE_PACKET_MAX_APID 0x07FFU
#define CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT 0x3FFFU

typedef enum {
    CCSDS_PACKET_TYPE_TELEMETRY = 0,
    CCSDS_PACKET_TYPE_COMMAND = 1
} ccsds_packet_type_t;

typedef enum {
    CCSDS_SEQUENCE_CONTINUATION = 0,
    CCSDS_SEQUENCE_FIRST = 1,
    CCSDS_SEQUENCE_LAST = 2,
    CCSDS_SEQUENCE_UNSEGMENTED = 3
} ccsds_sequence_flags_t;

typedef struct {
    ccsds_packet_type_t type;
    bool has_secondary_header;
    uint16_t apid;
    ccsds_sequence_flags_t sequence_flags;
    uint16_t sequence_count;
} ccsds_space_packet_header_t;

typedef struct {
    ccsds_space_packet_header_t header;
    const uint8_t *data;
    size_t data_length;
    size_t total_length;
} ccsds_space_packet_view_t;

ccsds_status_t ccsds_space_packet_write_header(
    const ccsds_space_packet_header_t *header,
    size_t data_length,
    uint8_t *output,
    size_t output_capacity
);

ccsds_status_t ccsds_space_packet_build(
    const ccsds_space_packet_header_t *header,
    const uint8_t *data,
    size_t data_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length
);

/* Read only the six-byte primary header and report the complete packet size. */
ccsds_status_t ccsds_space_packet_peek_size(
    const uint8_t *input,
    size_t input_length,
    size_t *packet_size
);

/* Parse one packet at the front of input. Extra following bytes are allowed. */
ccsds_status_t ccsds_space_packet_parse(
    const uint8_t *input,
    size_t input_length,
    ccsds_space_packet_view_t *view
);

#endif
