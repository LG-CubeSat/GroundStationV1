#include "flight_protocol.h"
#include "ccsds/profile.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(ok) do { if (!(ok)) { fprintf(stderr, "FAIL line %d\n", __LINE__); failures++; } } while (0)

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void putfloat(uint8_t *p, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(p, bits);
}

int main(void) {
    gs_command_request_t command = {.request_id = 0x12345678U, .kind = GS_COMMAND_ADCS_SAFE};
    uint8_t out[14];
    size_t length = 0;
    const uint8_t safe[14] = {2, 10, 64, 0, 0, 0, 0x78, 0x56,
                              0x34, 0x12, 1, 0, 0, 0};
    CHECK(gs_build_command(&command, out, sizeof(out), &length) == 0);
    CHECK(length == sizeof(safe) && memcmp(out, safe, sizeof(safe)) == 0);
    command.kind = GS_COMMAND_TAKE_PHOTO;
    CHECK(gs_build_command(&command, out, sizeof(out), &length) == 0);
    CHECK(length == 10 && out[0] == 1 && out[1] == 0 && out[2] == 5);
    command.kind = GS_COMMAND_INHIBIT_ACTUATORS;
    CHECK(gs_build_command(&command, out, sizeof(out), &length) == 0);
    CHECK(length == 14 && out[0] == 2 && out[2] == 68 && out[10] == 1);
    command.kind = GS_COMMAND_POINT_TO_SUN;
    CHECK(gs_build_command(&command, out, sizeof(out), &length) == 0);
    CHECK(length == 10 && out[0] == 2 && out[1] == 10 && out[2] == 7);
    command.kind = GS_COMMAND_RESET_IMU;
    CHECK(gs_build_command(&command, out, sizeof(out), &length) == 0);
    CHECK(length == 10 && out[0] == 2 && out[1] == 10 && out[2] == 6);
    command.kind = 0;
    CHECK(gs_build_command(&command, out, sizeof(out), &length) != 0 && length == 0);

    uint8_t record[202] = {0};
    memcpy(record, "WESW", 4);
    record[4] = 1;
    record[5] = 2;
    record[6] = 20;
    record[16] = 0;
    record[17] = 184;
    uint8_t *p = record + 18;
    memcpy(p, "ADCS", 4);
    p[4] = 3;
    p[5] = 1;
    put32(p + 8, 42);
    put32(p + 20, 3);
    putfloat(p + 60, 0.5F);
    putfloat(p + 112, 0.75F);
    putfloat(p + 180, 91.0F);
    gs_adcs_telemetry_t decoded;
    CHECK(gs_decode_adcs_record(record, sizeof(record), &decoded) == 0);
    CHECK(decoded.sequence == 42 && decoded.mode == 1 && decoded.sensor_valid_mask == 3);
    CHECK(fabsf(decoded.estimated_rate_rad_s[0] - 0.5F) < 1e-6F);
    CHECK(fabsf(decoded.confidence - 0.75F) < 1e-6F);
    CHECK(fabsf(decoded.minimum_stack_margin_words - 91.0F) < 1e-6F);
    uint8_t frame[240];
    size_t frame_length = 0;
    lg_ccsds_profile_view_t view;
    CHECK(lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY, 7, 1, NULL,
                                 record, sizeof(record), frame, sizeof(frame),
                                 &frame_length) == CCSDS_OK);
    CHECK(frame_length == 228 &&
          lg_ccsds_profile_parse(frame, frame_length, &view) == CCSDS_OK);
    CHECK(view.payload_length == sizeof(record) &&
          gs_decode_adcs_record(view.payload, view.payload_length, &decoded) == 0);
    record[18] = 'X';
    CHECK(gs_decode_adcs_record(record, sizeof(record), &decoded) != 0);
    record[18] = 'A';
    CHECK(gs_decode_adcs_record(record, sizeof(record) - 1, &decoded) != 0);
    const uint8_t ack_wire[14] = {
        'W','A','C','K', 1, 2, 10, 64, 0, 0, 0x12, 0x34, 0x56, 0x78
    };
    gs_command_result_t result;
    CHECK(gs_decode_command_result(ack_wire, sizeof(ack_wire), &result) == 0);
    CHECK(result.request_id == 0x12345678U && result.node == 2 &&
          result.command_id == 64 && result.status == GS_RESULT_ADCS_ACK);
    CHECK(gs_decode_command_result(ack_wire, sizeof(ack_wire) - 1, &result) != 0);
    if (failures) return 1;
    puts("flight_protocol_test: PASS");
    return 0;
}
