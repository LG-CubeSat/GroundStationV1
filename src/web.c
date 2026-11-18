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

typedef struct {
    nng_mtx *mutex;
    nng_socket commands;
    uint64_t packets;
    nng_time received_at;
    gs_adcs_telemetry_t telemetry;
    gs_command_result_t command_result;
    nng_time command_result_at;
    int have_command_result;
} web_state_t;

static volatile sig_atomic_t keep_running = 1;

// HTML FILE Absoloutely slopped together into here. In future: Make a script that automatically compiles an html file into here.
static const char index_html[] =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Flight Ground Station</title><style>"
    "body{font:16px system-ui,sans-serif;background:#101820;color:#e5edf5;max-width:1100px;margin:32px auto;padding:0 20px}"
    "h1{margin-bottom:4px}h2{margin-top:30px}p{color:#adc0d0}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}"
    ".card{background:#1c2b38;padding:16px;border-radius:8px;overflow-wrap:anywhere}"
    ".card strong{display:block;font-size:20px;margin-top:8px;font-weight:600}"
    "pre{background:#1c2b38;padding:16px;border-radius:8px;overflow:auto}"
    "button{background:#286b9c;color:white;border:0;border-radius:6px;padding:12px 16px;margin:0 8px 8px 0;cursor:pointer;font:inherit}"
    "button:disabled{opacity:.5;cursor:wait}button.safe{background:#326b4b}"
    "</style></head><body><h1>Flight Ground Station</h1>"
    "<p id='status' role='status'>Waiting for ADCS telemetry...</p>"
    "<div class='grid'>"
    "<div class='card'>Packets<strong id='packets'>0</strong></div>"
    "<div class='card'>RF received<strong id='age'>Never</strong></div>"
    "<div class='card'>OBC sample age<strong id='sample-age'>Never</strong></div>"
    "<div class='card'>ADCS mode<strong id='mode'>—</strong></div>"
    "<div class='card'>ADCS sequence<strong id='sequence'>—</strong></div>"
    "<div class='card'>Sensor validity<strong id='sensors'>—</strong></div>"
    "<div class='card'>Active faults<strong id='faults'>—</strong></div>"
    "<div class='card'>Estimated rate (rad/s)<strong id='rate'>—</strong></div>"
    "<div class='card'>Attitude quaternion<strong id='quaternion'>—</strong></div>"
    "<div class='card'>Magnetic field (T)<strong id='magnetic'>—</strong></div>"
    "<div class='card'>Sun vector<strong id='sun'>—</strong></div>"
    "<div class='card'>Confidence<strong id='confidence'>—</strong></div>"
    "<div class='card'>Control / health flags<strong id='flags'>—</strong></div>"
    "<div class='card'>Dropped messages<strong id='dropped'>—</strong></div>"
    "<div class='card'>Rejected commands<strong id='rejected'>—</strong></div>"
    "<div class='card'>Minimum stack margin<strong id='stack'>—</strong></div>"
    "</div><h2>Commands</h2>"
    "<p>Run the link check to verify RF uplink, ADCS acknowledgment, and fresh IMU-valid telemetry downlink.</p>"
    "<button class='safe' data-command='linkcheck'>Run end-to-end link check</button>"
    "<button class='safe' data-command='safe'>Set ADCS Safe</button>"
    "<button class='safe' data-command='inhibit'>Inhibit actuators</button>"
    "<button data-command='reset_imu'>Reset IMU</button>"
    "<button data-command='sun'>Point to Sun</button>"
    "<button data-command='photo'>Take photo</button>"
    "<p id='command-status' role='status'></p>"
    "<h2>Latest decoded record</h2><pre id='record'>Waiting...</pre>"
    "<script>const el=id=>document.getElementById(id);let lastRequest=null,linkCheck=null,lastPackets=0,lastSampleUs=0;"
    "const modes=['BOOT','SAFE','DETUMBLE','SUN ACQUISITION','SUN POINTING','EARTH POINTING','SLEWING','TARGET POINTING','SCIENCE'];"
    "const vec=a=>a.map(x=>Number.isFinite(x)?x.toPrecision(4):'—').join(', ');"
    "async function refresh(){try{const r=await fetch('/api/telemetry',{cache:'no-store',signal:AbortSignal.timeout(5000)});"
    "if(!r.ok)throw Error('HTTP '+r.status);const d=await r.json();"
    "lastPackets=d.packets;lastSampleUs=d.telemetry?.received_unix_us||0;"
    "el('packets').textContent=d.packets.toLocaleString();"
    "el('age').textContent=d.packets?(d.age_ms/1000).toFixed(1)+' s ago':'Never';"
    "const sampleAgeMs=d.telemetry?Date.now()-d.telemetry.received_unix_us/1000:Infinity;"
    "el('sample-age').textContent=!d.telemetry?'Never':"
    "sampleAgeMs < -2000?'Check Pi/Mac clocks':"
    "(Math.max(0,sampleAgeMs)/1000).toFixed(1)+' s';"
    "el('status').textContent=!d.packets?'Waiting for ADCS telemetry...':"
    "d.age_ms<5000&&sampleAgeMs>=-2000&&sampleAgeMs<10000?"
    "'Receiving recent ADCS telemetry':'Telemetry sample stale';"
    "if(d.telemetry){const t=d.telemetry;el('mode').textContent=modes[t.mode]||t.mode;"
    "el('sequence').textContent=t.sequence;"
    "el('sensors').textContent=['gyro','accel','mag','sun'].filter((_,i)=>t.sensor_valid_mask&(1<<i)).join(', ')||'none';"
    "el('faults').textContent='0x'+t.active_faults.toString(16);"
    "el('rate').textContent=vec(t.estimated_rate_rad_s);el('quaternion').textContent=vec(t.quaternion);"
    "el('magnetic').textContent=vec(t.magnetic_field_t);el('sun').textContent=vec(t.sun_vector_body);"
    "el('confidence').textContent=Number.isFinite(t.confidence)?t.confidence.toFixed(3):'—';"
    "el('flags').textContent='0x'+t.control_flags.toString(16)+' / 0x'+t.health_flags.toString(16);"
    "el('dropped').textContent=t.dropped_messages;el('rejected').textContent=t.rejected_commands;"
    "el('stack').textContent=Number.isFinite(t.minimum_stack_margin_words)?t.minimum_stack_margin_words.toFixed(0)+' words':'—';"
    "el('record').textContent=JSON.stringify(t,null,2);}"
    "if(lastRequest&&d.command_result&&d.command_result.request_id===lastRequest.id){"
    "const labels=['ADCS ACK','ADCS NACK','ADCS timeout','OBC route error','OBC queued'];"
    "el('command-status').textContent=lastRequest.name+': '+labels[d.command_result.status]+'.';"
    "lastRequest=null;}"
    "else if(lastRequest&&!linkCheck&&Date.now()>lastRequest.deadline){"
    "el('command-status').textContent=lastRequest.name+': no flight result within 45 s.';lastRequest=null;}"
    "if(linkCheck){const a=d.command_result;"
    "const ack=!!(a&&a.request_id===linkCheck.id&&a.status===0);"
    "const fresh=d.packets>linkCheck.startPackets&&!!d.telemetry&&"
    "d.telemetry.received_unix_us>linkCheck.startSampleUs&&"
    "sampleAgeMs>=-2000&&sampleAgeMs<10000&&"
    "d.telemetry.received_unix_us/1000>=linkCheck.sentAtMs-2000;"
    "const safe=fresh&&d.telemetry.mode===1;"
    "const imu=fresh&&(d.telemetry.sensor_valid_mask&3)===3;"
    "const progress='ADCS ACK '+(ack?'yes':'pending')+', fresh SAFE telemetry '+(safe?'yes':'pending')+"
    "', gyro/accel valid '+(imu?'yes':'pending')+'.';"
    "if(a&&a.request_id===linkCheck.id&&a.status!==0){el('command-status').textContent='Link check FAILED: '+"
    "['','ADCS NACK','ADCS reply timeout','OBC route error','OBC-only result'][a.status];linkCheck=null;}"
    "else if(ack&&safe&&imu){"
    "el('command-status').textContent='Link check PASS: ADCS ACK + fresh SAFE telemetry + gyro/accel valid.';linkCheck=null;}"
    "else if(Date.now()>linkCheck.deadline){el('command-status').textContent='Link check FAILED after 45 s: '+progress;linkCheck=null;}"
    "else el('command-status').textContent='Link check waiting: '+progress;}"
    "}catch(e){el('status').textContent='Web connection unavailable: '+e.message;}"
    "finally{setTimeout(refresh,500);}}refresh();"
    "document.querySelectorAll('[data-command]').forEach(button=>button.addEventListener('click',async()=>{"
    "button.disabled=true;try{const checking=button.dataset.command==='linkcheck';"
    "const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'text/plain'},"
    "body:checking?'safe':button.dataset.command,signal:AbortSignal.timeout(5000)});"
    "if(!r.ok)throw Error(await r.text());const d=await r.json();"
    "lastRequest={id:d.request_id,name:button.textContent,deadline:Date.now()+45000};"
    "linkCheck=checking?{id:d.request_id,startPackets:lastPackets,"
    "startSampleUs:lastSampleUs,sentAtMs:Date.now(),deadline:Date.now()+45000}:null;"
    "el('command-status').textContent=button.textContent+' queued (request '+d.request_id+').';"
    "}catch(e){el('command-status').textContent=e.message;}finally{button.disabled=false;}}));"
    "</script></body></html>";

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
                             index_html, sizeof(index_html) - 1U, head_only);

    static const char not_found[] = "Not found\n";
    return send_response(connection, 404, "Not Found", "text/plain",
                         not_found, sizeof(not_found) - 1U, head_only);
}

int main(void) {
    static web_state_t state;
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
    return result;
}
