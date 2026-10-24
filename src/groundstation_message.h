#ifndef GROUNDSTATION_MESSAGE_H
#define GROUNDSTATION_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Maximum CCSDS profile payload: 65536 data bytes - 16 header - 4 CRC. */
#define GROUNDSTATION_PACKET_DATA_CAPACITY 65516U
#define GROUNDSTATION_SSDV_PACKET_SIZE 256U

typedef enum {
    GROUNDSTATION_PACKET_TELEMETRY = 0,
    GROUNDSTATION_PACKET_SSDV = 2,
    GROUNDSTATION_PACKET_COMMAND = 3
} groundstation_packet_type_t;

typedef struct {
    uint32_t content_type;
    uint32_t sample_count;
    uint32_t samples_length;

    uint32_t bits_per_sample;
    uint32_t signed_samples;
    uint8_t samples[GROUNDSTATION_PACKET_DATA_CAPACITY];
} groundstation_telemetry_packet_t;

typedef struct {
    uint32_t content_type;
    uint8_t data[GROUNDSTATION_SSDV_PACKET_SIZE];
} groundstation_ssdv_packet_t;

typedef struct {
    uint32_t content_type;
    uint32_t command_count;
    uint32_t commands_length;
    uint8_t commands[GROUNDSTATION_PACKET_DATA_CAPACITY];
} groundstation_command_packet_t;

/* Every member starts with the same uint32_t content_type field. */
typedef union {
    groundstation_telemetry_packet_t telemetry;
    groundstation_ssdv_packet_t ssdv;
    groundstation_command_packet_t command;
} groundstation_packet_t;

static inline size_t groundstation_packet_size(uint32_t content_type) {
    switch (content_type) {
        case GROUNDSTATION_PACKET_TELEMETRY:
            return sizeof(groundstation_telemetry_packet_t);
        case GROUNDSTATION_PACKET_SSDV:
            return sizeof(groundstation_ssdv_packet_t);
        case GROUNDSTATION_PACKET_COMMAND:
            return sizeof(groundstation_command_packet_t);
        default:
            return 0U;
    }
}

static inline bool groundstation_packet_valid(const groundstation_packet_t *packet) {
    if (packet == NULL) {
        return false;
    }
    switch (packet->telemetry.content_type) {
        case GROUNDSTATION_PACKET_TELEMETRY: {
            const groundstation_telemetry_packet_t *telemetry = &packet->telemetry;
            size_t bytes_per_sample;

            if (telemetry->samples_length > sizeof(telemetry->samples) ||
                telemetry->bits_per_sample > 32U || telemetry->signed_samples > 1U) {
                return false;
            }
            if (telemetry->bits_per_sample == 0U) {
                return telemetry->signed_samples == 0U;
            }
            bytes_per_sample = (telemetry->bits_per_sample + 7U) / 8U;
            return telemetry->sample_count != 0U &&
                telemetry->sample_count <= sizeof(telemetry->samples) / bytes_per_sample &&
                telemetry->samples_length == telemetry->sample_count * bytes_per_sample;
        }
        case GROUNDSTATION_PACKET_SSDV:
            return true;
        case GROUNDSTATION_PACKET_COMMAND:
            return packet->command.commands_length <= sizeof(packet->command.commands);
        default:
            return false;
    }
}

static inline bool groundstation_packet_serialize(
    const groundstation_packet_t *packet, void *buffer, size_t capacity,
    size_t *written) {
    size_t packet_size;

    if (written == NULL) {
        return false;
    }
    *written = 0U;
    if (packet == NULL || buffer == NULL || !groundstation_packet_valid(packet)) {
        return false;
    }
    packet_size = groundstation_packet_size(packet->telemetry.content_type);
    if (capacity < packet_size) {
        return false;
    }
    memcpy(buffer, packet, packet_size);
    *written = packet_size;
    return true;
}

/* Accepts exactly one complete packet. Only use the result on success. */
static inline bool groundstation_packet_deserialize(
    groundstation_packet_t *packet, const void *buffer, size_t length) {
    uint32_t content_type;
    size_t packet_size;

    if (packet == NULL || buffer == NULL || length < sizeof(content_type)) {
        return false;
    }
    memcpy(&content_type, buffer, sizeof(content_type));
    packet_size = groundstation_packet_size(content_type);
    if (packet_size == 0U || length != packet_size) {
        return false;
    }
    memset(packet, 0, sizeof(*packet));
    memcpy(packet, buffer, packet_size);
    return groundstation_packet_valid(packet);
}

#endif
