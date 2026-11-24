#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <civetweb.h>
#include <nng/nng.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/sub.h>

#include "groundstation_message.h"
#include "flight_protocol.h"

#define HTTP_ADDRESS "127.0.0.1:8080"
#define UI_HTML_ADDRESS "src/slop.html"

typedef struct {
    nng_mtx *mutex;
    nng_socket commands;
    uint64_t packets;
    nng_time received_at;
    gs_adcs_telemetry_t telemetry;
    gs_command_result_t command_result;
    nng_time command_result_at;
    int have_command_result;
    char *html_ptr;
    size_t html_length;
} web_state_t;

static volatile sig_atomic_t keep_running = 1;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static int send_response(struct mg_connection *connection, int status,
                         const char *reason, const char *content_type,
                         const char *body, size_t length, int head_only) {
    if (mg_printf(connection,
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %zu\r\n"
                  "Cache-Control: no-store\r\n"
                  "X-Content-Type-Options: nosniff\r\n"
                  "Connection: close\r\n\r\n",
                  status, reason, content_type, length) > 0 && !head_only) {
        size_t written = 0;
        while (written < length) {
            int count = mg_write(connection, body + written, length - written);
            if (count <= 0) break;
            written += (size_t)count;
        }
    }
    return status;
}

static int appendf(char *out, size_t cap, size_t *used, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsnprintf(out + *used, cap - *used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= cap - *used) return -1;
    *used += (size_t)count;
    return 0;
}

static int append_vec(char *out, size_t cap, size_t *used, const char *name,
                      const float *values, size_t count) {
    if (appendf(out, cap, used, "\"%s\":[", name) != 0) return -1;
    for (size_t i = 0; i < count; ++i) {
        if (isfinite(values[i])) {
            if (appendf(out, cap, used, "%s%.9g", i ? "," : "", (double)values[i]) != 0) return -1;
        } else if (appendf(out, cap, used, "%snull", i ? "," : "") != 0) return -1;
    }
    return appendf(out, cap, used, "],");
}

static int append_float(char *out, size_t cap, size_t *used,
                        const char *name, float value) {
    if (isfinite(value))
        return appendf(out, cap, used, "\"%s\":%.9g,", name, (double)value);
    return appendf(out, cap, used, "\"%s\":null,", name);
}

static int build_html(const char *filename, web_state_t *state) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open external HTML file");
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed to find end of HTML file");
        fclose(file);
        return -1;
    }

    long length = ftell(file);
    if (length < 0) {
        perror("Failed to find length of HTML file");
        fclose(file);
        return -1;
    }
    state->html_length = (size_t)length;
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Failed to find beginning of HTML file");
        fclose(file);
        return -1;
    }

    state->html_ptr = malloc(state->html_length + 1);
    if (state->html_ptr == NULL) {
        perror("Failed to allocate memory for HTML file");
        fclose(file);
        return -1;
    }
    size_t bytes_read = fread(state->html_ptr, 1, state->html_length, file);
    if (bytes_read < state->html_length) {
        if (ferror(file)) {
            perror("Failed to copy HTML file");
        } else if (feof(file)) {
            fprintf(stderr, "Read %zu bytes but expected %zu", bytes_read, state->html_length);
        }
        free(state->html_ptr);
        fclose(file);
        return -1;
    }
    state->html_ptr[state->html_length] = '\0';

    if (fclose(file) != 0) {
        perror("Failed to close HTML file");
        free(state->html_ptr);
        return -1;
    }

    return 0;
}

static int send_telemetry(struct mg_connection *connection, web_state_t *state,
                          int head_only) {
    gs_adcs_telemetry_t t;
    gs_command_result_t result;
    uint64_t packets, age_ms;
    uint64_t result_age_ms;
    int have_result;
    char json[4096];
    size_t used = 0;

    nng_mtx_lock(state->mutex);
    t = state->telemetry;
    packets = state->packets;
    age_ms = packets ? nng_clock() - state->received_at : 0;
    result = state->command_result;
    have_result = state->have_command_result;
    result_age_ms = have_result ? nng_clock() - state->command_result_at : 0;
    nng_mtx_unlock(state->mutex);

    if (appendf(json, sizeof(json), &used,
                "{\"packets\":%" PRIu64 ",\"age_ms\":%" PRIu64 ",\"telemetry\":",
                packets, age_ms) != 0) goto format_error;
    if (!packets) {
        if (appendf(json, sizeof(json), &used, "null") != 0) goto format_error;
    } else {
        if (appendf(json, sizeof(json), &used,
                    "{\"received_unix_us\":%" PRIu64 ",\"board_monotonic_us\":%" PRIu64
                    ",\"sequence\":%" PRIu32 ",\"mode\":%u,\"sensor_valid_mask\":%" PRIu32
                    ",\"active_faults\":%" PRIu32 ",\"attitude_valid\":%u,"
                    "\"control_flags\":%u,\"health_flags\":%u,",
                    t.received_unix_us, t.board_monotonic_us, t.sequence, t.mode,
                    t.sensor_valid_mask, t.active_faults, t.attitude_valid,
                    t.control_flags, t.health_flags) != 0) goto format_error;
        if (append_vec(json, sizeof(json), &used, "quaternion", t.quaternion, 4) != 0 ||
            append_vec(json, sizeof(json), &used, "target_quaternion", t.target_quaternion, 4) != 0 ||
            append_vec(json, sizeof(json), &used, "estimated_rate_rad_s", t.estimated_rate_rad_s, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "gyro_bias_rad_s", t.gyro_bias_rad_s, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "magnetic_field_t", t.magnetic_field_t, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "sun_vector_body", t.sun_vector_body, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "requested_torque_nm", t.requested_torque_nm, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "requested_dipole_a_m2", t.requested_dipole_a_m2, 3) != 0 ||
            append_vec(json, sizeof(json), &used, "achievable_torque_nm", t.achievable_torque_nm, 3) != 0) goto format_error;
        if (append_float(json, sizeof(json), &used, "sun_irradiance_w_m2", t.sun_irradiance_w_m2) != 0 ||
            append_float(json, sizeof(json), &used, "confidence", t.confidence) != 0 ||
            append_float(json, sizeof(json), &used, "pointing_error_rad", t.pointing_error_rad) != 0 ||
            append_float(json, sizeof(json), &used, "minimum_stack_margin_words", t.minimum_stack_margin_words) != 0)
            goto format_error;
        if (appendf(json, sizeof(json), &used,
                    "\"rejected_commands\":%" PRIu32
                    ",\"dropped_messages\":%" PRIu32 ",\"estimator_resets\":%" PRIu32
                    ",\"controller_errors\":%" PRIu32 ",\"mode_transitions\":%" PRIu32
                    "}",
                    t.rejected_commands, t.dropped_messages, t.estimator_resets,
                    t.controller_errors, t.mode_transitions) != 0) goto format_error;
    }
    if (!have_result) {
        if (appendf(json, sizeof(json), &used, ",\"command_result\":null}\n") != 0)
            goto format_error;
    } else {
        if (appendf(json, sizeof(json), &used,
                    ",\"command_result\":{\"request_id\":%" PRIu32
                    ",\"node\":%u,\"port\":%u,\"command_id\":%u,"
                    "\"status\":%u,\"age_ms\":%" PRIu64 "}}\n",
                    result.request_id, result.node, result.port,
                    result.command_id, result.status, result_age_ms) != 0)
            goto format_error;
    }
    return send_response(connection, 200, "OK", "application/json", json, used, head_only);
format_error:;
    static const char error[] = "Unable to format telemetry\n";
    return send_response(connection, 500, "Internal Server Error", "text/plain",
                         error, sizeof(error) - 1, head_only);
}

static int send_command(struct mg_connection *connection, web_state_t *state) {
    const struct mg_request_info *request = mg_get_request_info(connection);
    gs_command_request_t command = {.request_id = nng_random()};
    char name[32] = {0};
    char response[80];
    size_t received = 0;
    int count, rv;

    if (request->content_length <= 0 || request->content_length >= (long long)sizeof(name))
        goto invalid;
    while (received < (size_t)request->content_length) {
        count = mg_read(connection, name + received,
                        (size_t)request->content_length - received);
        if (count <= 0) break;
        received += (size_t)count;
    }
    if (received != (size_t)request->content_length ||
        memchr(name, '\0', received) != NULL)
        goto invalid;
    if (strcmp(name, "photo") == 0) command.kind = GS_COMMAND_TAKE_PHOTO;
    else if (strcmp(name, "safe") == 0) command.kind = GS_COMMAND_ADCS_SAFE;
    else if (strcmp(name, "sun") == 0) command.kind = GS_COMMAND_POINT_TO_SUN;
    else if (strcmp(name, "reset_imu") == 0) command.kind = GS_COMMAND_RESET_IMU;
    else if (strcmp(name, "inhibit") == 0) command.kind = GS_COMMAND_INHIBIT_ACTUATORS;
    else goto invalid;

    rv = nng_send(state->commands, &command, sizeof(command), NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        static const char error[] = "Command was not queued; GSMain may be unavailable\n";
        return send_response(connection, 503, "Service Unavailable", "text/plain",
                             error, sizeof(error) - 1U, 0);
    }
    count = snprintf(response, sizeof(response), "{\"request_id\":%" PRIu32 "}\n",
                     command.request_id);
    return send_response(connection, 202, "Accepted", "application/json",
                         response, (size_t)count, 0);
invalid:
    ;
    static const char error[] = "Unknown command\n";
    return send_response(connection, 400, "Bad Request", "text/plain",
                         error, sizeof(error) - 1U, 0);
}

static int handle_request(struct mg_connection *connection) {
    const struct mg_request_info *request = mg_get_request_info(connection);
    web_state_t *state = request->user_data;
    const char *uri = request->local_uri;
    int head_only = strcmp(request->request_method, "HEAD") == 0;

    if (uri != NULL && strcmp(uri, "/api/command") == 0) {
        if (strcmp(request->request_method, "POST") == 0)
            return send_command(connection, state);
        mg_printf(connection,
                  "HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\n"
                  "Content-Length: 0\r\nConnection: close\r\n\r\n");
        return 405;
    }

    if (strcmp(request->request_method, "GET") != 0 && !head_only) {
        mg_printf(connection,
                  "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\n"
                  "Content-Length: 0\r\nConnection: close\r\n\r\n");
        return 405;
    }
    if (uri != NULL && strcmp(uri, "/api/telemetry") == 0)
        return send_telemetry(connection, state, head_only);
    if (uri != NULL && (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0))
        return send_response(connection, 200, "OK", "text/html; charset=utf-8",
                             state->html_ptr, state->html_length, head_only);

    static const char not_found[] = "Not found\n";
    return send_response(connection, 404, "Not Found", "text/plain",
                         not_found, sizeof(not_found) - 1U, head_only);
}

int main(void) {
    static web_state_t state = {.html_length = 0};
    struct mg_callbacks callbacks = {.begin_request = handle_request};
    struct mg_context *server = NULL;
    const char *http_address = getenv("GS_HTTP_ADDRESS");
    if (http_address == NULL || http_address[0] == '\0') http_address = HTTP_ADDRESS;
    const char *options[] = {
        "listening_ports", http_address,
        "num_threads", "2",
        "request_timeout_ms", "5000",
        NULL
    };
    nng_socket subscriber = NNG_SOCKET_INITIALIZER;
    int subscriber_open = 0;
    int commands_open = 0;
    int http_initialized = 0;
    int result = EXIT_FAILURE;
    int rv;

    (void)signal(SIGINT, request_shutdown);
    (void)signal(SIGTERM, request_shutdown);

    if ((rv = nng_mtx_alloc(&state.mutex)) != 0) goto nng_error;
    if ((rv = nng_sub0_open(&subscriber)) != 0) goto nng_error;
    subscriber_open = 1;
    if ((rv = nng_socket_set(subscriber, NNG_OPT_SUB_SUBSCRIBE, "", 0)) != 0)
        goto nng_error;
    if ((rv = nng_socket_set_size(subscriber, NNG_OPT_RECVMAXSZ,
                                   sizeof(gs_adcs_telemetry_t))) != 0)
        goto nng_error;
    if ((rv = nng_socket_set_ms(subscriber, NNG_OPT_RECVTIMEO, 250)) != 0)
        goto nng_error;

    if ((rv = nng_dial(subscriber, GROUNDSTATION_NNG_URL, NULL, NNG_FLAG_NONBLOCK)) != 0)
        goto nng_error;
    if ((rv = nng_push0_open(&state.commands)) != 0) goto nng_error;
    commands_open = 1;
    if ((rv = nng_dial(state.commands, GROUNDSTATION_COMMAND_NNG_URL,
                       NULL, NNG_FLAG_NONBLOCK)) != 0)
        goto nng_error;

    if (build_html(UI_HTML_ADDRESS, &state) != 0) {
        fprintf(stderr, "Failed to build web UI from %s\n", UI_HTML_ADDRESS);
        goto cleanup;
    }

    (void)mg_init_library(0);
    http_initialized = 1;
    server = mg_start(&callbacks, &state, options);
    if (server == NULL) {
        fprintf(stderr, "Unable to start CivetWeb on %s\n", http_address);
        goto cleanup;
    }
    fprintf(stderr, "Ground Station UI: http://%s\nReceiving: SUB %s\n",
            http_address, GROUNDSTATION_NNG_URL);

    while (keep_running) {
        nng_msg *message = NULL;
        gs_adcs_telemetry_t telemetry;
        rv = nng_recvmsg(subscriber, &message, 0);
        if (rv == NNG_ETIMEDOUT || rv == NNG_EINTR) continue;
        if (rv != 0) goto nng_error;

        size_t length = nng_msg_len(message);
        if (length == sizeof(telemetry)) {
            memcpy(&telemetry, nng_msg_body(message), sizeof(telemetry));
            nng_mtx_lock(state.mutex);
            if (state.packets == 0 ||
                telemetry.received_unix_us > state.telemetry.received_unix_us) {
                state.telemetry = telemetry;
                state.received_at = nng_clock();
            }
            state.packets++;
            nng_mtx_unlock(state.mutex);
        } else if (length == sizeof(gs_command_result_t)) {
            gs_command_result_t result_event;
            memcpy(&result_event, nng_msg_body(message), sizeof(result_event));
            nng_mtx_lock(state.mutex);
            state.command_result = result_event;
            state.have_command_result = 1;
            state.command_result_at = nng_clock();
            nng_mtx_unlock(state.mutex);
        } else {
            fprintf(stderr, "Dropping NNG telemetry of size %zu; expected %zu-byte struct\n",
                    length, sizeof(telemetry));
        }
        nng_msg_free(message);
    }
    result = EXIT_SUCCESS;
    goto cleanup;

nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(rv));
cleanup:
    if (server != NULL) mg_stop(server);
    if (http_initialized) (void)mg_exit_library();
    if (commands_open) (void)nng_close(state.commands);
    if (subscriber_open) (void)nng_close(subscriber);
    if (state.mutex != NULL) nng_mtx_free(state.mutex);
    if (state.html_length > 0) free(state.html_ptr);
    return result;
}
