#include "ccsds/stream.h"

#include <string.h>

#include "ccsds/space_packet.h"

ccsds_status_t ccsds_stream_parser_init(
    ccsds_stream_parser_t *parser,
    uint8_t *storage,
    size_t storage_capacity)
{
    if (parser == NULL || storage == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if (storage_capacity < CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }

    parser->storage = storage;
    parser->capacity = storage_capacity;
    parser->buffered = 0U;
    parser->expected_packet_size = 0U;
    return CCSDS_OK;
}

void ccsds_stream_parser_reset(ccsds_stream_parser_t *parser)
{
    if (parser != NULL) {
        parser->buffered = 0U;
        parser->expected_packet_size = 0U;
    }
}

ccsds_status_t ccsds_stream_parser_feed(
    ccsds_stream_parser_t *parser,
    const uint8_t *bytes,
    size_t byte_count,
    ccsds_packet_callback_t callback,
    void *user_context,
    size_t *packets_delivered)
{
    size_t offset = 0U;
    size_t delivered = 0U;

    if (parser == NULL || callback == NULL || packets_delivered == NULL ||
        (bytes == NULL && byte_count != 0U)) {
        return CCSDS_ERROR_NULL;
    }
    if (parser->storage == NULL ||
        parser->capacity < CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE ||
        parser->buffered > parser->capacity) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }

    while (offset < byte_count) {
        size_t target_size;
        size_t copy_size;

        if (parser->buffered < CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE) {
            target_size = CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE;
        } else {
            ccsds_status_t status;

            if (parser->expected_packet_size == 0U) {
                status = ccsds_space_packet_peek_size(
                    parser->storage, parser->buffered,
                    &parser->expected_packet_size);
                if (status != CCSDS_OK ||
                    parser->expected_packet_size > parser->capacity) {
                    ccsds_stream_parser_reset(parser);
                    return status == CCSDS_OK ?
                        CCSDS_ERROR_BUFFER_TOO_SMALL : status;
                }
            }
            target_size = parser->expected_packet_size;
        }

        copy_size = target_size - parser->buffered;
        if (copy_size > byte_count - offset) {
            copy_size = byte_count - offset;
        }
        memcpy(parser->storage + parser->buffered, bytes + offset, copy_size);
        parser->buffered += copy_size;
        offset += copy_size;

        if (parser->buffered == CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE &&
            parser->expected_packet_size == 0U) {
            ccsds_status_t status = ccsds_space_packet_peek_size(
                parser->storage, parser->buffered,
                &parser->expected_packet_size);
            if (status != CCSDS_OK ||
                parser->expected_packet_size > parser->capacity) {
                ccsds_stream_parser_reset(parser);
                return status == CCSDS_OK ?
                    CCSDS_ERROR_BUFFER_TOO_SMALL : status;
            }
        }

        if (parser->expected_packet_size != 0U &&
            parser->buffered == parser->expected_packet_size) {
            const ccsds_status_t callback_status = callback(
                parser->storage, parser->buffered, user_context);
            ccsds_stream_parser_reset(parser);
            if (callback_status != CCSDS_OK) {
                *packets_delivered = delivered;
                return CCSDS_ERROR_CALLBACK;
            }
            ++delivered;
        }
    }

    *packets_delivered = delivered;
    return CCSDS_OK;
}
