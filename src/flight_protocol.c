#include "flight_protocol.h"

#include <string.h>

#define OBC_RECORD_SIZE 18U
#define ADCS_RECORD_SIZE 184U

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static float befloat(const uint8_t *p) {
    uint32_t bits = be32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void floats(float *out, const uint8_t *p, size_t count) {
    for (size_t i = 0; i < count; ++i) out[i] = befloat(p + 4 * i);
}

int gs_build_command(const gs_command_request_t *request, uint8_t *out,
                     size_t capacity, size_t *written) {
    uint8_t node, port, command_id;
    size_t length = 10U;
    if (written != NULL) *written = 0;
    if (request == NULL || out == NULL || written == NULL) return -1;
    if (request->reserved[0] || request->reserved[1] || request->reserved[2]) return -1;
    switch (request->kind) {
    case GS_COMMAND_TAKE_PHOTO: node = 1; port = 0; command_id = 5; break;
    case GS_COMMAND_ADCS_SAFE: node = 2; port = 10; command_id = 64; length = 14U; break;
    case GS_COMMAND_POINT_TO_SUN: node = 2; port = 10; command_id = 7; break;
    case GS_COMMAND_RESET_IMU: node = 2; port = 10; command_id = 6; break;
    case GS_COMMAND_INHIBIT_ACTUATORS: node = 2; port = 10; command_id = 68; length = 14U; break;
    default: return -1;
    }
    if (capacity < length) return -1;
    memset(out, 0, length);
    out[0] = node;
    out[1] = port;
    out[2] = command_id;
    /* Current flight command_envelope_t is 8 bytes on the Pi/STM32 ABI:
       command_id, three padding bytes, little-endian uint32 sequence. */
    out[6] = (uint8_t)request->request_id;
    out[7] = (uint8_t)(request->request_id >> 8);
    out[8] = (uint8_t)(request->request_id >> 16);
    out[9] = (uint8_t)(request->request_id >> 24);
    if (request->kind == GS_COMMAND_ADCS_SAFE ||
        request->kind == GS_COMMAND_INHIBIT_ACTUATORS) out[10] = 1;
    *written = length;
    return 0;
}

int gs_decode_adcs_record(const uint8_t *record, size_t length,
                          gs_adcs_telemetry_t *out) {
    const uint8_t *p;
    if (record == NULL || out == NULL || length != OBC_RECORD_SIZE + ADCS_RECORD_SIZE ||
        memcmp(record, "WESW", 4) != 0 || record[4] != 1 ||
        record[5] != 2 || record[6] != 20 || record[7] != 0 ||
        be16(record + 16) != ADCS_RECORD_SIZE) return -1;
    p = record + OBC_RECORD_SIZE;
    if (memcmp(p, "ADCS", 4) != 0 || p[4] != 3 || p[5] > 8 ||
        be16(p + 6) > 1 || p[158] != 0 || p[159] != 0) return -1;

    memset(out, 0, sizeof(*out));
    out->received_unix_us = be64(record + 8);
    out->mode = p[5];
    out->attitude_valid = (uint8_t)be16(p + 6);
    out->sequence = be32(p + 8);
    out->board_monotonic_us = be64(p + 12);
    out->sensor_valid_mask = be32(p + 20);
    out->active_faults = be32(p + 24);
    floats(out->quaternion, p + 28, 4);
    floats(out->target_quaternion, p + 44, 4);
    floats(out->estimated_rate_rad_s, p + 60, 3);
    floats(out->gyro_bias_rad_s, p + 72, 3);
    floats(out->magnetic_field_t, p + 84, 3);
    floats(out->sun_vector_body, p + 96, 3);
    out->sun_irradiance_w_m2 = befloat(p + 108);
    out->confidence = befloat(p + 112);
    floats(out->requested_torque_nm, p + 116, 3);
    floats(out->requested_dipole_a_m2, p + 128, 3);
    floats(out->achievable_torque_nm, p + 140, 3);
    out->pointing_error_rad = befloat(p + 152);
    out->control_flags = p[156];
    out->health_flags = p[157];
    out->rejected_commands = be32(p + 160);
    out->dropped_messages = be32(p + 164);
    out->estimator_resets = be32(p + 168);
    out->controller_errors = be32(p + 172);
    out->mode_transitions = be32(p + 176);
    out->minimum_stack_margin_words = befloat(p + 180);
    return 0;
}
