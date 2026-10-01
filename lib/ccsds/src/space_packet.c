#include "ccsds/space_packet.h"

#include <string.h>

static ccsds_status_t validate_header(const ccsds_space_packet_header_t *header)
{
    if (header == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if ((header->type != CCSDS_PACKET_TYPE_TELEMETRY &&
         header->type != CCSDS_PACKET_TYPE_COMMAND) ||
        header->apid > CCSDS_SPACE_PACKET_MAX_APID ||
        header->sequence_flags > CCSDS_SEQUENCE_UNSEGMENTED ||
        header->sequence_count > CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT) {
        return CCSDS_ERROR_RANGE;
    }
    return CCSDS_OK;
}

ccsds_status_t ccsds_space_packet_write_header(
    const ccsds_space_packet_header_t *header,
    size_t data_length,
    uint8_t *output,
    size_t output_capacity)
{
    uint16_t packet_id;
    uint16_t sequence_control;
    uint16_t encoded_length;
    ccsds_status_t status;

    if (output == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = validate_header(header);
    if (status != CCSDS_OK) {
        return status;
    }
    if (data_length == 0U || data_length > CCSDS_SPACE_PACKET_MAX_DATA_SIZE) {
        return CCSDS_ERROR_RANGE;
    }
    if (output_capacity < CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }

    /* Packet Version Number is zero for CCSDS Space Packets. */
    packet_id = (uint16_t)(((uint16_t)header->type << 12U) |
                           ((header->has_secondary_header ? 1U : 0U) << 11U) |
                           header->apid);
    sequence_control = (uint16_t)(((uint16_t)header->sequence_flags << 14U) |
                                  header->sequence_count);
    encoded_length = (uint16_t)(data_length - 1U);

    output[0] = (uint8_t)(packet_id >> 8U);
    output[1] = (uint8_t)packet_id;
    output[2] = (uint8_t)(sequence_control >> 8U);
    output[3] = (uint8_t)sequence_control;
    output[4] = (uint8_t)(encoded_length >> 8U);
    output[5] = (uint8_t)encoded_length;
    return CCSDS_OK;
}

ccsds_status_t ccsds_space_packet_build(
    const ccsds_space_packet_header_t *header,
    const uint8_t *data,
    size_t data_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    size_t total_length;
    ccsds_status_t status;

    if (data == NULL || output == NULL || output_length == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if (data_length == 0U || data_length > CCSDS_SPACE_PACKET_MAX_DATA_SIZE) {
        return CCSDS_ERROR_RANGE;
    }
    total_length = CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE + data_length;
    if (output_capacity < total_length) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }

    status = ccsds_space_packet_write_header(
        header, data_length, output, output_capacity);
    if (status != CCSDS_OK) {
        return status;
    }

    memcpy(output + CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE, data, data_length);
    *output_length = total_length;
    return CCSDS_OK;
}

ccsds_status_t ccsds_space_packet_peek_size(
    const uint8_t *input,
    size_t input_length,
    size_t *packet_size)
{
    uint16_t encoded_length;

    if (input == NULL || packet_size == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if (input_length < CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }
    if ((input[0] & 0xE0U) != 0U) {
        return CCSDS_ERROR_UNSUPPORTED;
    }

    encoded_length = (uint16_t)(((uint16_t)input[4] << 8U) | input[5]);
    *packet_size = CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE +
                   (size_t)encoded_length + 1U;
    return CCSDS_OK;
}

ccsds_status_t ccsds_space_packet_parse(
    const uint8_t *input,
    size_t input_length,
    ccsds_space_packet_view_t *view)
{
    uint16_t packet_id;
    uint16_t sequence_control;
    size_t packet_size;
    ccsds_status_t status;

    if (input == NULL || view == NULL) {
        return CCSDS_ERROR_NULL;
    }

    status = ccsds_space_packet_peek_size(input, input_length, &packet_size);
    if (status != CCSDS_OK) {
        return status;
    }
    if (input_length < packet_size) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }

    packet_id = (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
    sequence_control = (uint16_t)(((uint16_t)input[2] << 8U) | input[3]);

    view->header.type = (ccsds_packet_type_t)((packet_id >> 12U) & 1U);
    view->header.has_secondary_header = ((packet_id >> 11U) & 1U) != 0U;
    view->header.apid = packet_id & CCSDS_SPACE_PACKET_MAX_APID;
    view->header.sequence_flags =
        (ccsds_sequence_flags_t)((sequence_control >> 14U) & 3U);
    view->header.sequence_count =
        sequence_control & CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT;
    view->data = input + CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE;
    view->data_length = packet_size - CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE;
    view->total_length = packet_size;
    return CCSDS_OK;
}
