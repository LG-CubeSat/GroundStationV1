#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <civetweb.h>
#include <nng/nng.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/sub.h>

#include "groundstation_message.h"
#include "serial.h"

#define HTTP_ADDRESS "127.0.0.1:8080"

typedef struct {
    nng_mtx *mutex;
    nng_socket commands;
    uint64_t packets;
    uint64_t bytes;
    nng_time received_at;
    groundstation_telemetry_packet_t telemetry;
} web_state_t;

static volatile sig_atomic_t keep_running = 1;

// HTML FILE Absoloutely slopped together into here. In future: Make a script that automatically compiles an html file into here.
static const char index_html[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Ground Station</title><style>"
    "body{font:16px system-ui,sans-serif;background:#101820;color:#e5edf5;"
    "max-width:1000px;margin:40px auto;padding:0 20px}"
    "h1{margin-bottom:8px}p{color:#adc0d0}"
    ".stats{display:flex;flex-wrap:wrap;gap:16px;margin:24px 0}"
    ".card{background:#1c2b38;padding:20px;border-radius:8px;flex:1;min-width:120px}"
    ".card strong{display:block;font-size:26px;margin-top:8px}"
    "pre{background:#1c2b38;padding:20px;border-radius:8px;overflow:auto;"
    "white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.5}"
    "#status{color:#8cd9ac}code{overflow-wrap:anywhere}"
    "</style></head><body><h1>Ground Station</h1>"
    "<p>Live telemetry</p>"
    "<p id=\"status\" role=\"status\">Waiting for the first packet...</p>"
    "<div class=\"stats\"><div class=\"card\">Packets<strong id=\"packets\">0</strong></div>"
    "<div class=\"card\">Total bytes<strong id=\"bytes\">0</strong></div>"
    "<div class=\"card\">Latest size<strong id=\"length\">0 B</strong></div>"
    "<div class=\"card\">Last received<strong id=\"age\">Never</strong></div></div>"
    "<div class=\"stats\"><div class=\"card\">Sample<strong id=\"sample\">—</strong></div>"
    "<div class=\"card\">Temperature<strong id=\"temperature\">—</strong></div>"
    "<div class=\"card\">Battery<strong id=\"battery\">—</strong></div>"
    "<div class=\"card\">Altitude<strong id=\"altitude\">—</strong></div></div>"
    "<h2>Latest payload: hex</h2><pre id=\"hex\">No data received.</pre>"
    "<h2>Send command</h2><form id=\"command-form\">"
    "<label for=\"command\">Command</label> "
    "<input id=\"command\" maxlength=\"63\" value=\"PING\" required> "
    "<button type=\"submit\">Send</button></form>"
    "<p id=\"command-status\" role=\"status\"></p>"
    "<script>const el=id=>document.getElementById(id);"
    "async function refresh(){try{"
    "const r=await fetch('/api/telemetry',{cache:'no-store',"
    "signal:AbortSignal.timeout(5000)});"
    "if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();"
    "el('packets').textContent=d.packets.toLocaleString();"
    "el('bytes').textContent=d.bytes.toLocaleString();"
    "el('length').textContent=d.length+' B';"
    "el('age').textContent=d.packets?(d.age_ms/1000).toFixed(1)+' s ago':'Never';"
    "el('status').textContent=!d.packets?'Waiting for the first packet...':"
    "d.age_ms<3000?'Receiving payloads':'No recent payloads; waiting...';"
    "if(d.telemetry){const t=d.telemetry;"
    "el('sample').textContent=t.sample;"
    "el('temperature').textContent=Number.isFinite(t.temperature_c)?"
    "t.temperature_c.toFixed(2)+' °C':'Unavailable';"
    "el('battery').textContent=Number.isFinite(t.battery_v)?"
    "t.battery_v.toFixed(2)+' V':'Unavailable';"
    "el('altitude').textContent=t.altitude_m+' m';"
    "const b=d.hex.match(/../g)||[];let rows=[];"
    "for(let i=0;i<b.length;i+=16)rows.push(i.toString(16).padStart(4,'0')"
    "+'  '+b.slice(i,i+16).join(' '));"
    "el('hex').textContent=rows.join('\\n');}else{"
    "for(const id of ['sample','temperature','battery','altitude'])el(id).textContent='—';"
    "el('hex').textContent='No data received.';}"
    "}catch(e){el('status').textContent='Web connection unavailable; retrying...';}"
    "finally{setTimeout(refresh,500);}}refresh();"
    "el('command-form').addEventListener('submit',async e=>{e.preventDefault();"
    "const button=e.target.querySelector('button');button.disabled=true;try{"
    "const r=await fetch('/api/command',{method:'POST',"
    "headers:{'Content-Type':'text/plain;charset=utf-8'},body:el('command').value,"
    "signal:AbortSignal.timeout(5000)});"
    "if(!r.ok)throw new Error(await r.text());const d=await r.json();"
    "el('command-status').textContent='Command '+d.request_id+' queued for GSMain.';"
    "}catch(error){el('command-status').textContent=error.message;}"
    "finally{button.disabled=false;}});</script></body></html>";

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

static int send_telemetry(struct mg_connection *connection, web_state_t *state,
                          int head_only) {
    static const char digits[] = "0123456789abcdef";
    static const char error[] = "Unable to format telemetry\n";
    groundstation_telemetry_packet_t telemetry;
    char json[1024];
    char fields[384] = "null";
    char hex[sizeof(telemetry) * 2U + 1U] = "";
    char temperature[32];
    char battery[32];
    uint64_t packets, bytes, age_ms;
    int count;

    nng_mtx_lock(state->mutex);
    memcpy(&telemetry, &state->telemetry, sizeof(telemetry));
    packets = state->packets;
    bytes = state->bytes;
    age_ms = packets ? nng_clock() - state->received_at : 0U;
    nng_mtx_unlock(state->mutex);

    if (packets != 0U) {
        uint8_t payload[sizeof(telemetry)];
        size_t written;
        if (serialize_telemetry(&telemetry, payload, sizeof(payload), &written) != SERIAL_OK)
            goto format_error;
        for (size_t i = 0; i < written; ++i) {
            hex[i * 2U] = digits[payload[i] >> 4];
            hex[i * 2U + 1U] = digits[payload[i] & 15U];
        }

        if (isfinite(telemetry.temperature_c))
            (void)snprintf(temperature, sizeof(temperature), "%.9g", (double)telemetry.temperature_c);
        else
            (void)strcpy(temperature, "null");
        if (isfinite(telemetry.battery_v))
            (void)snprintf(battery, sizeof(battery), "%.9g", (double)telemetry.battery_v);
        else
            (void)strcpy(battery, "null");
        count = snprintf(fields, sizeof(fields),
                         "{\"test_run\":%" PRIu32 ",\"sample\":%" PRIu32
                         ",\"temperature_c\":%s,\"battery_v\":%s,\"altitude_m\":%" PRId32 "}",
                         telemetry.test_run, telemetry.sample, temperature, battery, telemetry.altitude_m);
        if (count < 0 || (size_t)count >= sizeof(fields)) goto format_error;
    }
    count = snprintf(json, sizeof(json),
                     "{\"packets\":%" PRIu64 ",\"bytes\":%" PRIu64
                     ",\"length\":%zu,\"age_ms\":%" PRIu64 ",\"hex\":\"%s\",\"telemetry\":%s}\n",
                     packets, bytes, packets ? sizeof(telemetry) : (size_t)0,
                     age_ms, hex, fields);
    if (count < 0 || (size_t)count >= sizeof(json)) goto format_error;
    return send_response(connection, 200, "OK", "application/json",
                         json, (size_t)count, head_only);

format_error:
    return send_response(connection, 500, "Internal Server Error", "text/plain",
                         error, sizeof(error) - 1U, head_only);
}

static int send_command(struct mg_connection *connection, web_state_t *state) {
    const struct mg_request_info *request = mg_get_request_info(connection);
    groundstation_command_packet_t command = {.request_id = nng_random()};
    uint8_t payload[sizeof(command)];
    char response[80];
    size_t received = 0U, written = 0U;
    int count, rv;

    if (request->content_length <= 0 ||
        request->content_length >= (long long)sizeof(command.command)) {
        static const char error[] = "Command must contain 1..63 UTF-8 bytes\n";
        return send_response(connection, 400, "Bad Request", "text/plain",
                             error, sizeof(error) - 1U, 0);
    }
    while (received < (size_t)request->content_length) {
        count = mg_read(connection, command.command + received,
                        (size_t)request->content_length - received);
        if (count <= 0) break;
        received += (size_t)count;
    }
    if (received != (size_t)request->content_length ||
        memchr(command.command, '\0', received) != NULL) {
        static const char error[] = "Incomplete command or embedded NUL byte\n";
        return send_response(connection, 400, "Bad Request", "text/plain",
                             error, sizeof(error) - 1U, 0);
    }
    if (serialize_command(&command, payload, sizeof(payload), &written) != SERIAL_OK) {
        static const char error[] = "Unable to serialize command\n";
        return send_response(connection, 500, "Internal Server Error", "text/plain",
                             error, sizeof(error) - 1U, 0);
    }
    rv = nng_send(state->commands, payload, written, NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        static const char error[] = "Command was not queued; GSMain may be unavailable\n";
        return send_response(connection, 503, "Service Unavailable", "text/plain",
                             error, sizeof(error) - 1U, 0);
    }
    count = snprintf(response, sizeof(response), "{\"request_id\":%" PRIu32 "}\n",
                     command.request_id);
    return send_response(connection, 202, "Accepted", "application/json",
                         response, (size_t)count, 0);
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
                             index_html, sizeof(index_html) - 1U, head_only);

    static const char not_found[] = "Not found\n";
    return send_response(connection, 404, "Not Found", "text/plain",
                         not_found, sizeof(not_found) - 1U, head_only);
}

int main(void) {
    static web_state_t state;
    struct mg_callbacks callbacks = {.begin_request = handle_request};
    struct mg_context *server = NULL;
    const char *options[] = {
        "listening_ports", HTTP_ADDRESS,
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
                                   sizeof(groundstation_telemetry_packet_t))) != 0)
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

    (void)mg_init_library(0);
    http_initialized = 1;
    server = mg_start(&callbacks, &state, options);
    if (server == NULL) {
        fprintf(stderr, "Unable to start CivetWeb on %s\n", HTTP_ADDRESS);
        goto cleanup;
    }
    fprintf(stderr, "Ground Station UI: http://%s\nReceiving: SUB %s\n",
            HTTP_ADDRESS, GROUNDSTATION_NNG_URL);

    while (keep_running) {
        nng_msg *message = NULL;
        groundstation_telemetry_packet_t telemetry;
        rv = nng_recvmsg(subscriber, &message, 0);
        if (rv == NNG_ETIMEDOUT || rv == NNG_EINTR) continue;
        if (rv != 0) goto nng_error;

        size_t length = nng_msg_len(message);
        if (deserialize_telemetry(nng_msg_body(message), length, &telemetry) == SERIAL_OK) {
            nng_mtx_lock(state.mutex);
            memcpy(&state.telemetry, &telemetry, sizeof(telemetry));
            state.packets++;
            state.bytes += length;
            state.received_at = nng_clock();
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
    return result;
}
