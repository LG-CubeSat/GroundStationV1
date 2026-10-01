#include "ccsds/profile.h"

#include <stdbool.h>
#include <string.h>

#include "ccsds/crc32.h"

#define PROFILE_FLAG_SIGNED 0x01U
#define PROFILE_FLAG_PREPROCESS 0x02U
#define PROFILE_FLAG_RESTRICTED 0x04U
#define PROFILE_KNOWN_FLAGS \
    (PROFILE_FLAG_SIGNED | PROFILE_FLAG_PREPROCESS | PROFILE_FLAG_RESTRICTED)

static void write_u16_be(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint16_t read_u16_be(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24U) |
           ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) |
           input[3];
}

static ccsds_status_t route_for_content(
    lg_ccsds_content_type_t content_type,
    uint16_t *apid,
    ccsds_packet_type_t *packet_type)
{
    if (apid == NULL || packet_type == NULL) {
        return CCSDS_ERROR_NULL;
    }

    *packet_type = CCSDS_PACKET_TYPE_TELEMETRY;
    switch (content_type) {
        case LG_CCSDS_CONTENT_RAW_TELEMETRY:
            *apid = LG_CCSDS_APID_RAW_TELEMETRY;
            return CCSDS_OK;
        case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY:
            *apid = LG_CCSDS_APID_COMPRESSED_TELEMETRY;
            return CCSDS_OK;
        case LG_CCSDS_CONTENT_SSDV:
            *apid = LG_CCSDS_APID_SSDV;
            return CCSDS_OK;
        case LG_CCSDS_CONTENT_COMMAND:
            *apid = LG_CCSDS_APID_COMMAND;
            *packet_type = CCSDS_PACKET_TYPE_COMMAND;
            return CCSDS_OK;
        default:
            return CCSDS_ERROR_UNSUPPORTED;
    }
}

static uint8_t config_flags(const ccsds_121_config_t *config)
{
    uint8_t flags = 0U;

    if (config->signed_samples) {
        flags |= PROFILE_FLAG_SIGNED;
    }
    if (config->preprocess) {
        flags |= PROFILE_FLAG_PREPROCESS;
    }
    if (config->restricted_code_options) {
        flags |= PROFILE_FLAG_RESTRICTED;
    }
    return flags;
}

static ccsds_status_t validate_content(
    lg_ccsds_content_type_t content_type,
    uint32_t item_count,
    const ccsds_121_config_t *compression,
    size_t payload_length)
{
    uint16_t unused_apid;
    ccsds_packet_type_t unused_packet_type;
    ccsds_status_t status = route_for_content(
        content_type, &unused_apid, &unused_packet_type);

    if (status != CCSDS_OK) {
        return status;
    }
    if (content_type == LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY) {
        if (compression == NULL || item_count == 0U || payload_length == 0U) {
            return CCSDS_ERROR_RANGE;
        }
        return ccsds_121_validate_config(compression);
    }
    if (compression != NULL) {
        return CCSDS_ERROR_RANGE;
    }
    if (content_type == LG_CCSDS_CONTENT_SSDV &&
        (payload_length != LG_CCSDS_SSDV_PACKET_SIZE || item_count != 1U)) {
        return CCSDS_ERROR_RANGE;
    }
    return CCSDS_OK;
}

ccsds_status_t lg_ccsds_profile_build(
    lg_ccsds_content_type_t content_type,
    uint16_t sequence_count,
    uint32_t item_count,
    const ccsds_121_config_t *compression,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    ccsds_space_packet_header_t packet_header;
    uint16_t apid;
    ccsds_packet_type_t packet_type;
    uint8_t *profile_header;
    size_t data_length;
    size_t total_length;
    uint32_t crc;
    ccsds_status_t status;

    if (payload == NULL || output == NULL || output_length == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if (sequence_count > CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT ||
        payload_length > LG_CCSDS_PROFILE_MAX_PAYLOAD_SIZE) {
        return CCSDS_ERROR_RANGE;
    }
    data_length = LG_CCSDS_PROFILE_HEADER_SIZE +
                  payload_length + LG_CCSDS_PROFILE_CRC_SIZE;
    total_length = CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE + data_length;
    if (output_capacity < total_length) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }
    status = validate_content(
        content_type, item_count, compression, payload_length);
    if (status != CCSDS_OK) {
        return status;
    }
    status = route_for_content(content_type, &apid, &packet_type);
    if (status != CCSDS_OK) {
        return status;
    }

    packet_header.type = packet_type;
    packet_header.has_secondary_header = true;
    packet_header.apid = apid;
    packet_header.sequence_flags = CCSDS_SEQUENCE_UNSEGMENTED;
    packet_header.sequence_count = sequence_count;
    status = ccsds_space_packet_write_header(
        &packet_header, data_length, output, output_capacity);
    if (status != CCSDS_OK) {
        return status;
    }

    profile_header = output + CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE;
    memset(profile_header, 0, LG_CCSDS_PROFILE_HEADER_SIZE);
    profile_header[0] = LG_CCSDS_PROFILE_VERSION;
    profile_header[1] = (uint8_t)content_type;
    if (compression != NULL) {
        profile_header[2] = config_flags(compression);
        profile_header[3] = compression->bits_per_sample;
        profile_header[4] = compression->block_size;
        write_u16_be(profile_header + 5U, compression->reference_sample_interval);
    }
    write_u32_be(profile_header + 8U, item_count);
    write_u32_be(profile_header + 12U, (uint32_t)payload_length);

    memcpy(profile_header + LG_CCSDS_PROFILE_HEADER_SIZE, payload, payload_length);
    crc = ccsds_crc32(output, total_length - LG_CCSDS_PROFILE_CRC_SIZE);
    write_u32_be(output + total_length - LG_CCSDS_PROFILE_CRC_SIZE, crc);
    *output_length = total_length;
    return CCSDS_OK;
}

static ccsds_status_t parse_compression_config(
    const uint8_t *profile_header,
    ccsds_121_config_t *config)
{
    if ((profile_header[2] & (uint8_t)~PROFILE_KNOWN_FLAGS) != 0U ||
        profile_header[7] != 0U) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    config->bits_per_sample = profile_header[3];
    config->block_size = profile_header[4];
    config->reference_sample_interval = read_u16_be(profile_header + 5U);
    config->signed_samples = (profile_header[2] & PROFILE_FLAG_SIGNED) != 0U;
    config->preprocess = (profile_header[2] & PROFILE_FLAG_PREPROCESS) != 0U;
    config->restricted_code_options =
        (profile_header[2] & PROFILE_FLAG_RESTRICTED) != 0U;
    return ccsds_121_validate_config(config);
}

ccsds_status_t lg_ccsds_profile_parse(
    const uint8_t *packet,
    size_t packet_length,
    lg_ccsds_profile_view_t *view)
{
    ccsds_space_packet_view_t space_packet;
    const uint8_t *profile_header;
    lg_ccsds_content_type_t content_type;
    uint16_t expected_apid;
    ccsds_packet_type_t expected_packet_type;
    uint32_t transmitted_crc;
    uint32_t calculated_crc;
    uint32_t payload_length;
    ccsds_121_config_t compression;
    const ccsds_121_config_t *compression_ptr = NULL;
    ccsds_status_t status;

    if (packet == NULL || view == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = ccsds_space_packet_parse(packet, packet_length, &space_packet);
    if (status != CCSDS_OK) {
        return status;
    }
    if (space_packet.total_length != packet_length ||
        !space_packet.header.has_secondary_header ||
        space_packet.header.sequence_flags != CCSDS_SEQUENCE_UNSEGMENTED ||
        space_packet.data_length <
            LG_CCSDS_PROFILE_HEADER_SIZE + LG_CCSDS_PROFILE_CRC_SIZE) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    transmitted_crc = read_u32_be(packet + packet_length - LG_CCSDS_PROFILE_CRC_SIZE);
    calculated_crc = ccsds_crc32(packet, packet_length - LG_CCSDS_PROFILE_CRC_SIZE);
    if (transmitted_crc != calculated_crc) {
        return CCSDS_ERROR_CRC;
    }

    profile_header = space_packet.data;
    if (profile_header[0] != LG_CCSDS_PROFILE_VERSION) {
        return CCSDS_ERROR_UNSUPPORTED;
    }
    if (profile_header[7] != 0U) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }
    content_type = (lg_ccsds_content_type_t)profile_header[1];
    status = route_for_content(
        content_type, &expected_apid, &expected_packet_type);
    if (status != CCSDS_OK) {
        return status;
    }
    if (space_packet.header.apid != expected_apid ||
        space_packet.header.type != expected_packet_type) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    payload_length = read_u32_be(profile_header + 12U);
    if ((size_t)payload_length != space_packet.data_length -
                                  LG_CCSDS_PROFILE_HEADER_SIZE -
                                  LG_CCSDS_PROFILE_CRC_SIZE) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    memset(&compression, 0, sizeof(compression));
    if (content_type == LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY) {
        status = parse_compression_config(profile_header, &compression);
        if (status != CCSDS_OK) {
            return status == CCSDS_ERROR_RANGE ? CCSDS_ERROR_INVALID_FORMAT : status;
        }
        compression_ptr = &compression;
    } else if (profile_header[2] != 0U || profile_header[3] != 0U ||
               profile_header[4] != 0U || profile_header[5] != 0U ||
               profile_header[6] != 0U) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    status = validate_content(
        content_type,
        read_u32_be(profile_header + 8U),
        compression_ptr,
        payload_length);
    if (status != CCSDS_OK) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    view->space_packet = space_packet;
    view->metadata.content_type = content_type;
    view->metadata.sequence_count = space_packet.header.sequence_count;
    view->metadata.item_count = read_u32_be(profile_header + 8U);
    view->metadata.compression = compression;
    view->payload = profile_header + LG_CCSDS_PROFILE_HEADER_SIZE;
    view->payload_length = payload_length;
    view->crc32 = transmitted_crc;
    return CCSDS_OK;
}

const char *lg_ccsds_content_type_name(lg_ccsds_content_type_t content_type)
{
    switch (content_type) {
        case LG_CCSDS_CONTENT_RAW_TELEMETRY:
            return "raw telemetry";
        case LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY:
            return "CCSDS 121 compressed telemetry";
        case LG_CCSDS_CONTENT_SSDV:
            return "SSDV image packet";
        case LG_CCSDS_CONTENT_COMMAND:
            return "command";
        default:
            return "unknown";
    }
}
