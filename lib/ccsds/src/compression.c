#include "ccsds/compression.h"

#include <limits.h>
#include <string.h>

#include "libaec.h"

static ccsds_status_t checked_sample_bytes(
    const ccsds_121_config_t *config,
    size_t sample_count,
    size_t *byte_count)
{
    size_t storage_size;
    ccsds_status_t status;

    if (byte_count == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = ccsds_121_bytes_per_sample(config, &storage_size);
    if (status != CCSDS_OK) {
        return status;
    }
    if (sample_count == 0U || sample_count > SIZE_MAX / storage_size) {
        return CCSDS_ERROR_RANGE;
    }
    *byte_count = sample_count * storage_size;
    return CCSDS_OK;
}

static unsigned int libaec_flags(const ccsds_121_config_t *config)
{
    unsigned int flags = AEC_DATA_MSB;

    if (config->signed_samples) {
        flags |= AEC_DATA_SIGNED;
    }
    if (config->preprocess) {
        flags |= AEC_DATA_PREPROCESS;
    }
    if (config->restricted_code_options) {
        flags |= AEC_RESTRICTED;
    }
    if (config->bits_per_sample > 16U && config->bits_per_sample <= 24U) {
        flags |= AEC_DATA_3BYTE;
    }
    return flags;
}

static ccsds_status_t map_encode_status(int status)
{
    switch (status) {
        case AEC_OK:
            return CCSDS_OK;
        case AEC_CONF_ERROR:
            return CCSDS_ERROR_RANGE;
        case AEC_MEM_ERROR:
            return CCSDS_ERROR_BUFFER_TOO_SMALL;
        default:
            return CCSDS_ERROR_CODEC;
    }
}

static ccsds_status_t map_decode_status(int status)
{
    switch (status) {
        case AEC_OK:
            return CCSDS_OK;
        case AEC_CONF_ERROR:
            return CCSDS_ERROR_RANGE;
        case AEC_MEM_ERROR:
            return CCSDS_ERROR_BUFFER_TOO_SMALL;
        case AEC_DATA_ERROR:
            return CCSDS_ERROR_INVALID_FORMAT;
        default:
            return CCSDS_ERROR_CODEC;
    }
}

ccsds_status_t ccsds_121_validate_config(const ccsds_121_config_t *config)
{
    bool legal_block_size;

    if (config == NULL) {
        return CCSDS_ERROR_NULL;
    }
    legal_block_size = config->block_size == 8U ||
                       config->block_size == 16U ||
                       config->block_size == 32U ||
                       config->block_size == 64U;
    if (config->bits_per_sample < 1U || config->bits_per_sample > 32U ||
        !legal_block_size ||
        config->reference_sample_interval < 1U ||
        config->reference_sample_interval > 4096U ||
        (config->restricted_code_options && config->bits_per_sample > 4U)) {
        return CCSDS_ERROR_RANGE;
    }
    return CCSDS_OK;
}

ccsds_status_t ccsds_121_bytes_per_sample(
    const ccsds_121_config_t *config,
    size_t *bytes_per_sample)
{
    ccsds_status_t status;

    if (bytes_per_sample == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = ccsds_121_validate_config(config);
    if (status != CCSDS_OK) {
        return status;
    }

    if (config->bits_per_sample <= 8U) {
        *bytes_per_sample = 1U;
    } else if (config->bits_per_sample <= 16U) {
        *bytes_per_sample = 2U;
    } else if (config->bits_per_sample <= 24U) {
        *bytes_per_sample = 3U;
    } else {
        *bytes_per_sample = 4U;
    }
    return CCSDS_OK;
}

ccsds_status_t ccsds_121_encoded_bound(
    const ccsds_121_config_t *config,
    size_t sample_count,
    size_t *encoded_bound)
{
    size_t input_bytes;
    ccsds_status_t status;

    if (encoded_bound == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = checked_sample_bytes(config, sample_count, &input_bytes);
    if (status != CCSDS_OK) {
        return status;
    }

    if (input_bytes > (SIZE_MAX - 256U) / 67U) {
        return CCSDS_ERROR_RANGE;
    }
    *encoded_bound = (input_bytes * 67U) / 64U + 256U;
    return CCSDS_OK;
}

static ccsds_status_t validate_unused_sample_bits(
    const ccsds_121_config_t *config,
    const uint8_t *samples,
    size_t sample_count,
    size_t bytes_per_sample)
{
    const unsigned int used_bits_in_first_byte = config->bits_per_sample % 8U;
    size_t index;

    if (used_bits_in_first_byte == 0U) {
        return CCSDS_OK;
    }

    for (index = 0U; index < sample_count; ++index) {
        const uint8_t unused_mask =
            (uint8_t)(UINT8_MAX << used_bits_in_first_byte);
        if ((samples[index * bytes_per_sample] & unused_mask) != 0U) {
            return CCSDS_ERROR_RANGE;
        }
    }
    return CCSDS_OK;
}

static void clear_unused_sample_bits(
    const ccsds_121_config_t *config,
    uint8_t *samples,
    size_t sample_count,
    size_t bytes_per_sample)
{
    const unsigned int used_bits_in_first_byte = config->bits_per_sample % 8U;
    size_t index;

    if (used_bits_in_first_byte == 0U) {
        return;
    }

    for (index = 0U; index < sample_count; ++index) {
        const uint8_t used_mask = (uint8_t)((1U << used_bits_in_first_byte) - 1U);
        samples[index * bytes_per_sample] &= used_mask;
    }
}

ccsds_status_t ccsds_121_encode(
    const ccsds_121_config_t *config,
    const uint8_t *samples,
    size_t sample_count,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_length)
{
    struct aec_stream stream;
    size_t input_bytes;
    size_t bytes_per_sample;
    ccsds_status_t status;
    int codec_status;

    if (samples == NULL || encoded == NULL || encoded_length == NULL) {
        return CCSDS_ERROR_NULL;
    }
    status = checked_sample_bytes(config, sample_count, &input_bytes);
    if (status != CCSDS_OK) {
        return status;
    }
    status = ccsds_121_bytes_per_sample(config, &bytes_per_sample);
    if (status != CCSDS_OK) {
        return status;
    }
    status = validate_unused_sample_bits(
        config, samples, sample_count, bytes_per_sample);
    if (status != CCSDS_OK) {
        return status;
    }
    if (encoded_capacity == 0U) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }

    memset(&stream, 0, sizeof(stream));
    stream.next_in = samples;
    stream.avail_in = input_bytes;
    stream.next_out = encoded;
    stream.avail_out = encoded_capacity;
    stream.bits_per_sample = config->bits_per_sample;
    stream.block_size = config->block_size;
    stream.rsi = config->reference_sample_interval;
    stream.flags = libaec_flags(config);

    codec_status = aec_buffer_encode(&stream);
    status = map_encode_status(codec_status);
    if (status != CCSDS_OK) {
        return status;
    }
    *encoded_length = stream.total_out;
    return CCSDS_OK;
}

ccsds_status_t ccsds_121_decode(
    const ccsds_121_config_t *config,
    const uint8_t *encoded,
    size_t encoded_length,
    size_t expected_sample_count,
    uint8_t *samples,
    size_t samples_capacity,
    size_t *samples_length)
{
    struct aec_stream stream;
    size_t output_bytes;
    size_t bytes_per_sample;
    ccsds_status_t status;
    int codec_status;

    if (encoded == NULL || samples == NULL || samples_length == NULL) {
        return CCSDS_ERROR_NULL;
    }
    if (encoded_length == 0U) {
        return CCSDS_ERROR_RANGE;
    }
    status = checked_sample_bytes(config, expected_sample_count, &output_bytes);
    if (status != CCSDS_OK) {
        return status;
    }
    if (samples_capacity < output_bytes) {
        return CCSDS_ERROR_BUFFER_TOO_SMALL;
    }
    status = ccsds_121_bytes_per_sample(config, &bytes_per_sample);
    if (status != CCSDS_OK) {
        return status;
    }

    memset(&stream, 0, sizeof(stream));
    stream.next_in = encoded;
    stream.avail_in = encoded_length;
    stream.next_out = samples;
    stream.avail_out = output_bytes;
    stream.bits_per_sample = config->bits_per_sample;
    stream.block_size = config->block_size;
    stream.rsi = config->reference_sample_interval;
    stream.flags = libaec_flags(config);

    codec_status = aec_buffer_decode(&stream);
    status = map_decode_status(codec_status);
    if (status != CCSDS_OK) {
        return status;
    }
    if (stream.total_out != output_bytes) {
        return CCSDS_ERROR_INVALID_FORMAT;
    }
    /* libaec sign-extends narrow signed values on output. The mission profile
     * uses one canonical wire representation with unused high bits cleared. */
    clear_unused_sample_bits(
        config, samples, expected_sample_count, bytes_per_sample);
    *samples_length = stream.total_out;
    return CCSDS_OK;
}
