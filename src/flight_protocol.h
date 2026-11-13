#ifndef GS_FLIGHT_PROTOCOL_H
#define GS_FLIGHT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* NNG messages are local to the ground laptop. RF bytes are built separately. */
typedef enum {
    GS_COMMAND_TAKE_PHOTO = 1,
    GS_COMMAND_ADCS_SAFE = 2,
    GS_COMMAND_POINT_TO_SUN = 3,
    GS_COMMAND_RESET_IMU = 4,
    GS_COMMAND_INHIBIT_ACTUATORS = 5
} gs_command_kind_t;

typedef struct {
    uint32_t request_id;
    uint8_t kind;
    uint8_t reserved[3];
} gs_command_request_t;

typedef struct {
    uint64_t received_unix_us;
    uint64_t board_monotonic_us;
    uint32_t sequence;
    uint32_t sensor_valid_mask;
    uint32_t active_faults;
    uint8_t mode;
    uint8_t attitude_valid;
    uint8_t control_flags;
    uint8_t health_flags;
    float quaternion[4];
    float target_quaternion[4];
    float estimated_rate_rad_s[3];
    float gyro_bias_rad_s[3];
    float magnetic_field_t[3];
    float sun_vector_body[3];
    float sun_irradiance_w_m2;
    float confidence;
    float requested_torque_nm[3];
    float requested_dipole_a_m2[3];
    float achievable_torque_nm[3];
    float pointing_error_rad;
    uint32_t rejected_commands;
    uint32_t dropped_messages;
    uint32_t estimator_resets;
    uint32_t controller_errors;
    uint32_t mode_transitions;
    float minimum_stack_margin_words;
} gs_adcs_telemetry_t;

/* Returns an RF command payload: destination node, CSP port, then command. */
int gs_build_command(const gs_command_request_t *request, uint8_t *out,
                     size_t capacity, size_t *written);

/* Decodes the WESW OBC record and its ADCS v3 payload. */
int gs_decode_adcs_record(const uint8_t *record, size_t length,
                          gs_adcs_telemetry_t *out);

#endif
