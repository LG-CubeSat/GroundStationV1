#ifndef CCSDS_COMPRESSION_H
#define CCSDS_COMPRESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ccsds/status.h"

/* CCSDS 121.0-B-3 supports sample resolutions from 1 through 32 bits. */
typedef struct {
    uint8_t bits_per_sample;
    uint8_t block_size;
    uint16_t reference_sample_interval;
    bool signed_samples;
    bool preprocess;
    bool restricted_code_options;
} ccsds_121_config_t;

ccsds_status_t ccsds_121_validate_config(const ccsds_121_config_t *config);

/* Number of whole bytes used to store one sample in the LG-CubeSat profile. */
ccsds_status_t ccsds_121_bytes_per_sample(
    const ccsds_121_config_t *config,
    size_t *bytes_per_sample
);

/* Conservative libaec bound: input * 67 / 64 + 256, overflow checked. */
ccsds_status_t ccsds_121_encoded_bound(
    const ccsds_121_config_t *config,
    size_t sample_count,
    size_t *encoded_bound
);

/*
 * Samples use network byte order (most-significant byte first).  When a
 * sample width does not fill its storage bytes, the unused high bits must be
 * zero. Signed samples use an n-bit two's-complement value in the low n bits.
 */
ccsds_status_t ccsds_121_encode(
    const ccsds_121_config_t *config,
    const uint8_t *samples,
    size_t sample_count,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_length
);

ccsds_status_t ccsds_121_decode(
    const ccsds_121_config_t *config,
    const uint8_t *encoded,
    size_t encoded_length,
    size_t expected_sample_count,
    uint8_t *samples,
    size_t samples_capacity,
    size_t *samples_length
);

#endif
