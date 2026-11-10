#ifndef CCSDS_PROFILE_H
#define CCSDS_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "ccsds/compression.h"
#include "ccsds/space_packet.h"
#include "ccsds/status.h"

/* One project-level format keeps the flight and ground implementations aligned. */
#define LG_CCSDS_PROFILE_VERSION 1U
#define LG_CCSDS_PROFILE_HEADER_SIZE 16U
#define LG_CCSDS_PROFILE_CRC_SIZE 4U
#define LG_CCSDS_PROFILE_MAX_PAYLOAD_SIZE \
    (CCSDS_SPACE_PACKET_MAX_DATA_SIZE - LG_CCSDS_PROFILE_HEADER_SIZE - \
     LG_CCSDS_PROFILE_CRC_SIZE)
#define LG_CCSDS_SSDV_PACKET_SIZE 256U

/* APIDs are mission-managed values in the CCSDS Space Packet Protocol. */
#define LG_CCSDS_APID_RAW_TELEMETRY 0x001U
#define LG_CCSDS_APID_COMPRESSED_TELEMETRY 0x002U
#define LG_CCSDS_APID_SSDV 0x003U
#define LG_CCSDS_APID_COMMAND 0x100U

typedef enum {
    LG_CCSDS_CONTENT_RAW_TELEMETRY = 0,
    LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY = 1,
    LG_CCSDS_CONTENT_SSDV = 2,
    LG_CCSDS_CONTENT_COMMAND = 3
} lg_ccsds_content_type_t;

typedef struct {
    lg_ccsds_content_type_t content_type;
    uint16_t sequence_count;
    uint32_t item_count;
    ccsds_121_config_t compression;
} lg_ccsds_profile_metadata_t;

typedef struct {
    ccsds_space_packet_view_t space_packet;
    lg_ccsds_profile_metadata_t metadata;
    const uint8_t *payload;
    size_t payload_length;
    uint32_t crc32;
} lg_ccsds_profile_view_t;

/*
 * Build one complete, unsegmented Space Packet.  The APID and packet type are
 * selected from content_type so callers cannot accidentally create a command
 * using a telemetry APID.  compression is used only for compressed telemetry.
 */
ccsds_status_t lg_ccsds_profile_build(
    lg_ccsds_content_type_t content_type,
    uint16_t sequence_count,
    uint32_t item_count,
    const ccsds_121_config_t *compression,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length
);

/* Parse and verify one exact packet, including the profile CRC-32. */
ccsds_status_t lg_ccsds_profile_parse(
    const uint8_t *packet,
    size_t packet_length,
    lg_ccsds_profile_view_t *view
);

const char *lg_ccsds_content_type_name(lg_ccsds_content_type_t content_type);

#endif
