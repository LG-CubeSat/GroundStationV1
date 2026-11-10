// sloppity slop test exe

/*
 * Run GSMain and GSEyecandy first, then GSTest [--count N] [--command PING].
 * --count 0 keeps supplying telemetry until Ctrl+C; --skip-web tests only NNG.
 * This program is both a simulated radio peer and a separate NNG application.
 * All payload encoding/decoding uses the same serial.h/.c as the other apps.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <civetweb.h>
#include <nng/nng.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/sub.h>

#include "ccsds/ccsds.h"
#include "groundstation_message.h"
#include "serial.h"

#define RADIO_INPUT_PORT 9000
#define RADIO_OUTPUT_PORT 9001
#define CHECK_TIMEOUT_MS 5000U

static volatile sig_atomic_t keep_running = 1;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static int open_radio_peer(void) {
    struct sockaddr_in address = {0};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int flags;
    if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        (void)close(fd);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(RADIO_OUTPUT_PORT);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

static int check_command(int radio, nng_socket commands, const char *text) {
    groundstation_command_packet_t sent = {.request_id = nng_random()};
    uint8_t payload[sizeof(sent)];
    uint8_t packet[CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE + sizeof(sent) + 1U];
    size_t written;
    nng_time deadline;
    int rv;

    if (text[0] == '\0' || strlen(text) >= sizeof(sent.command)) {
        fprintf(stderr, "Command must contain 1..%zu bytes\n", sizeof(sent.command) - 1U);
        return -1;
    }
    memcpy(sent.command, text, strlen(text));
    if (serialize_command(&sent, payload, sizeof(payload), &written) != SERIAL_OK) return -1;
    rv = nng_send(commands, payload, written, 0);
    if (rv != 0) {
        fprintf(stderr, "Sending NNG command: %s\n", nng_strerror(rv));
        return -1;
    }
    deadline = nng_clock() + CHECK_TIMEOUT_MS;
    while (keep_running && nng_clock() < deadline) {
        struct sockaddr_in source = {0};
        socklen_t source_length = sizeof(source);
        ssize_t length = recvfrom(radio, packet, sizeof(packet), 0,
                                  (struct sockaddr *)&source, &source_length);
        if (length < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("Receiving radio command");
                return -1;
            }
        } else if (source.sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
                   source.sin_port == htons(RADIO_INPUT_PORT)) {
            ccsds_space_packet_view_t view;
            groundstation_command_packet_t received;
            if (ccsds_space_packet_parse(packet, (size_t)length, &view) == CCSDS_OK &&
                view.total_length == (size_t)length &&
                view.header.type == CCSDS_PACKET_TYPE_COMMAND &&
                view.header.apid == LG_CCSDS_APID_COMMAND &&
                !view.header.has_secondary_header &&
                view.header.sequence_flags == CCSDS_SEQUENCE_UNSEGMENTED &&
                deserialize_command(view.data, view.data_length, &received) == SERIAL_OK &&
                received.request_id == sent.request_id &&
                memcmp(view.data, payload, written) == 0) {
                printf("PASS NNG -> GSMain -> CCSDS -> radio -> deserialize_command: %s\n", text);
                return 0;
            }
        }
        nng_msleep(10);
    }
    fprintf(stderr, "Command did not return on UDP port %d\n", RADIO_OUTPUT_PORT);
    return -1;
}

/* Check the browser-facing response without duplicating the payload schema. */
static int web_has_payload(const uint8_t *payload, size_t length) {
    static const char digits[] = "0123456789abcdef";
    char expected[sizeof(groundstation_telemetry_packet_t) * 2U + 16U];
    char body[2048], error[160];
    struct mg_connection *connection;
    const struct mg_response_info *response;
    size_t used = 0U;
    size_t body_length;
    int result = 0;

    if (length != sizeof(groundstation_telemetry_packet_t)) return 0;
    used = (size_t)snprintf(expected, sizeof(expected), "\"hex\":\"");
    for (size_t i = 0; i < length; ++i) {
        expected[used++] = digits[payload[i] >> 4];
        expected[used++] = digits[payload[i] & 15U];
    }
    expected[used++] = '"';
    expected[used] = '\0';
    connection = mg_connect_client("127.0.0.1", 8080, 0, error, sizeof(error));
    if (connection == NULL) return 0;
    if (mg_printf(connection, "GET /api/telemetry HTTP/1.1\r\n"
                              "Host: 127.0.0.1:8080\r\nConnection: close\r\n\r\n") <= 0 ||
        mg_get_response(connection, error, sizeof(error), 1000) < 0)
        goto cleanup;
    response = mg_get_response_info(connection);
    if (response == NULL || response->status_code != 200 || response->content_length < 0 ||
        response->content_length >= (long long)sizeof(body))
        goto cleanup;
    body_length = (size_t)response->content_length;
    used = 0U;
    while (used < body_length) {
        int count = mg_read(connection, body + used, body_length - used);
        if (count <= 0) goto cleanup;
        used += (size_t)count;
    }
    body[used] = '\0';
    result = strstr(body, expected) != NULL;
cleanup:
    mg_close_connection(connection);
    return result;
}

static int check_telemetry(int radio, nng_socket subscriber,
                           const groundstation_telemetry_packet_t *telemetry, int check_web) {
    uint8_t payload[sizeof(*telemetry)];
    uint8_t packet[CCSDS_SPACE_PACKET_PRIMARY_HEADER_SIZE + sizeof(payload)];
    size_t written, packet_length;
    ccsds_space_packet_header_t header = {
        .type = CCSDS_PACKET_TYPE_TELEMETRY,
        .apid = LG_CCSDS_APID_RAW_TELEMETRY,
        .sequence_flags = CCSDS_SEQUENCE_UNSEGMENTED,
        .sequence_count = (uint16_t)(telemetry->sample & CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT)
    };
    struct sockaddr_in destination = {0};
    nng_time deadline = nng_clock() + CHECK_TIMEOUT_MS;
    nng_time send_at = 0U, web_at = 0U;
    int have_nng = 0, have_web = !check_web;

    if (serialize_telemetry(telemetry, payload, sizeof(payload), &written) != SERIAL_OK ||
        ccsds_space_packet_build(&header, payload, written, packet, sizeof(packet),
                                 &packet_length) != CCSDS_OK)
        return -1;
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    destination.sin_port = htons(RADIO_INPUT_PORT);
    while (keep_running && nng_clock() < deadline) {
        if (nng_clock() >= send_at) {
            ssize_t sent = sendto(radio, packet, packet_length, 0,
                                  (const struct sockaddr *)&destination, sizeof(destination));
            if (sent < 0 || (size_t)sent != packet_length) {
                perror("Sending radio telemetry");
                return -1;
            }
            send_at = nng_clock() + 100U;
        }
        for (unsigned int attempt = 0; attempt < 32U; ++attempt) {
            nng_msg *message = NULL;
            groundstation_telemetry_packet_t received;
            int rv = nng_recvmsg(subscriber, &message, NNG_FLAG_NONBLOCK);
            if (rv == NNG_EAGAIN) break;
            if (rv != 0) {
                fprintf(stderr, "Receiving NNG telemetry: %s\n", nng_strerror(rv));
                return -1;
            }
            if (deserialize_telemetry(nng_msg_body(message), nng_msg_len(message),
                                       &received) == SERIAL_OK &&
                received.test_run == telemetry->test_run && received.sample == telemetry->sample &&
                memcmp(nng_msg_body(message), payload, written) == 0)
                have_nng = 1;
            nng_msg_free(message);
        }
        if (have_nng && !have_web && nng_clock() >= web_at) {
            have_web = web_has_payload(payload, written);
            web_at = nng_clock() + 250U;
        }
        if (have_nng && have_web) {
            printf("PASS radio -> GSMain -> NNG%s: sample %" PRIu32
                   ", %.2f C, %.2f V, %" PRId32 " m\n",
                   check_web ? " -> web" : "", telemetry->sample,
                   (double)telemetry->temperature_c, (double)telemetry->battery_v, telemetry->altitude_m);
            fflush(stdout);
            return 0;
        }
        nng_msleep(10);
    }
    fprintf(stderr, "Telemetry timed out (NNG: %s; web: %s)\n",
            have_nng ? "received" : "missing", have_web ? "received/skipped" : "missing");
    return -1;
}

int main(int argc, char **argv) {
    nng_socket subscriber = NNG_SOCKET_INITIALIZER;
    nng_socket commands = NNG_SOCKET_INITIALIZER;
    int subscriber_open = 0, commands_open = 0, http_initialized = 0;
    int radio = -1, result = EXIT_FAILURE, rv, check_web = 1;
    unsigned long count = 10U;
    const char *command = "PING";
    uint32_t run = nng_random();

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--skip-web") == 0) check_web = 0;
        else if (strcmp(argv[i], "--command") == 0 && i + 1 < argc) command = argv[++i];
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            errno = 0;
            count = strtoul(value, &end, 10);
            if (errno != 0 || value[0] == '-' || end == value || *end != '\0' || count > UINT32_MAX)
                goto usage;
        } else {
            if (strcmp(argv[i], "--help") == 0) result = EXIT_SUCCESS;
            goto usage;
        }
    }
    (void)signal(SIGINT, request_shutdown);
    (void)signal(SIGTERM, request_shutdown);
    radio = open_radio_peer();
    if (radio < 0) {
        perror("Opening radio peer on UDP port 9001");
        goto cleanup;
    }
    if ((rv = nng_sub0_open(&subscriber)) != 0) goto nng_error;
    subscriber_open = 1;
    if ((rv = nng_socket_set(subscriber, NNG_OPT_SUB_SUBSCRIBE, "", 0)) != 0)
        goto nng_error;
    if ((rv = nng_socket_set_size(subscriber, NNG_OPT_RECVMAXSZ,
                                   GROUNDSTATION_PACKET_DATA_CAPACITY)) != 0)
        goto nng_error;
    if ((rv = nng_dial(subscriber, GROUNDSTATION_NNG_URL, NULL, NNG_FLAG_NONBLOCK)) != 0)
        goto nng_error;
    if ((rv = nng_push0_open(&commands)) != 0) goto nng_error;
    commands_open = 1;
    if ((rv = nng_socket_set_ms(commands, NNG_OPT_SENDTIMEO, 3000)) != 0)
        goto nng_error;
    if ((rv = nng_dial(commands, GROUNDSTATION_COMMAND_NNG_URL, NULL, NNG_FLAG_NONBLOCK)) != 0)
        goto nng_error;
    if (check_web) {
        (void)mg_init_library(0);
        http_initialized = 1;
    }
    if (check_command(radio, commands, command) != 0) goto cleanup;
    for (uint64_t completed = 0U; keep_running && (count == 0U || completed < count); ++completed) {
        uint32_t sample = (uint32_t)(completed + 1U);
        groundstation_telemetry_packet_t telemetry = {
            .test_run = run, .sample = sample,
            .temperature_c = 20.0f + (sample % 20U) * 0.25f,
            .battery_v = 4.2f - (sample % 10U) * 0.02f,
            .altitude_m = 1000 + (int32_t)(sample % 100000U) * 10
        };
        if (check_telemetry(radio, subscriber, &telemetry, check_web) != 0) goto cleanup;
        if (count == 0U || completed + 1U < count)
            for (int pause = 0; keep_running && pause < 100; ++pause) nng_msleep(10);
    }
    result = EXIT_SUCCESS;
    goto cleanup;
nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(rv));
cleanup:
    if (http_initialized) (void)mg_exit_library();
    if (commands_open) (void)nng_close(commands);
    if (subscriber_open) (void)nng_close(subscriber);
    if (radio >= 0) (void)close(radio);
    return result;
usage:
    fprintf(stderr, "Usage: %s [--count N] [--command TEXT] [--skip-web]\n"
                    "Run GSMain and GSEyecandy first; --count 0 runs until Ctrl+C.\n", argv[0]);
    return result;
}
