#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pubsub0/pub.h>

#include "ccsds/ccsds.h"
#include "groundstation_message.h"
#include "radio.h"
#include "../lib/SSDV/ssdv.h"

#define RADIO_BUFFER_SIZE UINT16_MAX

typedef struct {
    nng_socket publisher;
    ssdv_t decoder;
    ssdv_packet_info_t image;
    uint8_t *jpeg;
    int have_image;
    int image_finished;
    unsigned int file_number;
} receive_context_t;

static volatile sig_atomic_t keep_running = 1;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static int valid_payload(const void *data, size_t length) {
    /* Field validation belongs here once shared application structs exist. */
    return data != NULL && length > 0U && length <= GROUNDSTATION_PACKET_DATA_CAPACITY;
}

static void reset_image(receive_context_t *context) {
    free(context->jpeg);
    context->jpeg = NULL;
    context->have_image = 0;
    context->image_finished = 0;
}

static int same_image(const ssdv_packet_info_t *a, const ssdv_packet_info_t *b) {
    return a->callsign == b->callsign && a->image_id == b->image_id &&
        a->width == b->width && a->height == b->height && a->type == b->type &&
        a->quality == b->quality && a->mcu_mode == b->mcu_mode;
}

static int save_image(receive_context_t *context, const uint8_t *jpeg, size_t length) {
    char path[96];
    FILE *file = NULL;
    int saved_errno = 0;

    for (unsigned int attempt = 0; attempt < 1000U; ++attempt) {
        int count = snprintf(path, sizeof(path), "ssdv-%08" PRIx32 "-%03u-%u.jpg",
                             context->image.callsign, (unsigned int)context->image.image_id,
                             context->file_number++);
        if (count < 0 || (size_t)count >= sizeof(path)) {
            fprintf(stderr, "SSDV image filename is too long\n");
            return -1;
        }
        file = fopen(path, "wbx");
        if (file != NULL) break;
        if (errno != EEXIST) {
            perror("Creating SSDV image");
            return -1;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "Unable to find an unused SSDV image filename\n");
        return -1;
    }
    errno = 0;
    if (fwrite(jpeg, 1, length, file) != length) saved_errno = errno ? errno : EIO;
    if (fclose(file) != 0 && saved_errno == 0) saved_errno = errno ? errno : EIO;
    if (saved_errno != 0) {
        fprintf(stderr, "Writing %s failed: %s\n", path, strerror(saved_errno));
        if (remove(path) != 0) perror("Removing incomplete SSDV file");
        return -1;
    }
    fprintf(stderr, "Saved SSDV image: %s (%zu bytes)\n", path, length);
    return 0;
}

static void finish_image(receive_context_t *context, int recovered) {
    uint8_t *jpeg;
    size_t length;

    if (!context->have_image || context->image_finished) return;
    if (ssdv_dec_get_jpeg(&context->decoder, &jpeg, &length) != SSDV_OK ||
        context->decoder.out_len == 0U) {
        fprintf(stderr, "Unable to finalize SSDV image\n");
        reset_image(context);
        return;
    }
    if (recovered) fprintf(stderr, "Saving recovered SSDV image with missing blocks\n");
    (void)save_image(context, jpeg, length);
    free(context->jpeg);
    context->jpeg = NULL;
    context->image_finished = 1;
}

static void receive_ssdv(receive_context_t *context, const uint8_t *data, size_t length) {
    uint8_t packet[SSDV_PKT_SIZE];
    ssdv_packet_info_t info;
    int status;

    if (length != sizeof(packet)) {
        fprintf(stderr, "Dropping SSDV payload of invalid size: %zu\n", length);
        return;
    }
    memcpy(packet, data, sizeof(packet));
    if (ssdv_dec_is_packet(packet, sizeof(packet), NULL) != SSDV_OK) {
        fprintf(stderr, "Dropping invalid SSDV packet\n");
        return;
    }
    ssdv_dec_header(&info, packet);

    if (!context->have_image || !same_image(&context->image, &info)) {
        size_t mcus = (size_t)(info.width / 16U) * (info.height / 16U);
        size_t blocks = info.mcu_mode == 0U ? 6U : info.mcu_mode == 3U ? 3U : 4U;
        size_t capacity;

        if (info.packet_id != 0U && info.mcu_id == UINT16_MAX) return;
        if (info.mcu_mode == 1U || info.mcu_mode == 2U) mcus *= 2U;
        else if (info.mcu_mode == 3U) mcus *= 4U;
        if (mcus == 0U || mcus > UINT16_MAX) {
            fprintf(stderr, "SSDV image exceeds decoder MCU capacity\n");
            return;
        }

        finish_image(context, 1);
        reset_image(context);
        if (ssdv_dec_init(&context->decoder, SSDV_PKT_SIZE) != SSDV_OK) {
            fprintf(stderr, "Unable to initialize SSDV decoder\n");
            return;
        }

        capacity = 1024U + mcus * blocks * 512U;
        context->jpeg = malloc(capacity);
        if (context->jpeg == NULL) {
            perror("Allocating SSDV image");
            return;
        }
        context->decoder.out = context->decoder.outp = context->jpeg;
        if (ssdv_dec_set_buffer(&context->decoder, context->jpeg, capacity) != SSDV_OK) {
            fprintf(stderr, "Unable to set SSDV output buffer\n");
            reset_image(context);
            return;
        }
        context->image = info;
        context->have_image = 1;
    }

    if (context->image_finished || info.packet_id < context->decoder.packet_id) return;
    status = ssdv_dec_feed(&context->decoder, packet);
    if ((status != SSDV_OK && status != SSDV_FEED_ME) || context->decoder.out_len == 0U ||
        (status == SSDV_FEED_ME && context->decoder.packet_id == 0U)) {
        fprintf(stderr, "SSDV decoding failed or exceeded decoder capacity\n");
        reset_image(context);
        return;
    }
    if (status == SSDV_OK || info.eoi) {
        finish_image(context, status != SSDV_OK);
    }
}

static ccsds_status_t receive_packet(const uint8_t *packet, size_t length, void *user_context) {
    receive_context_t *context = user_context;
    ccsds_space_packet_view_t view;
    nng_msg *message;
    int rv;
    ccsds_status_t status = ccsds_space_packet_parse(packet, length, &view);

    if (status != CCSDS_OK) {
        fprintf(stderr, "Dropping invalid CCSDS packet: %s\n", ccsds_status_string(status));
        return CCSDS_OK;
    }
    if (view.total_length != length || view.header.has_secondary_header ||
        view.header.sequence_flags != CCSDS_SEQUENCE_UNSEGMENTED ||
        !valid_payload(view.data, view.data_length)) {
        fprintf(stderr, "Dropping CCSDS packet with unexpected framing or payload length\n");
        return CCSDS_OK;
    }
    switch (view.header.apid) {
    case LG_CCSDS_APID_RAW_TELEMETRY:
    case LG_CCSDS_APID_COMPRESSED_TELEMETRY:
    case LG_CCSDS_APID_SSDV:
        if (view.header.type != CCSDS_PACKET_TYPE_TELEMETRY) {
            fprintf(stderr, "Dropping telemetry APID with command packet type\n");
            return CCSDS_OK;
        }
        break;
    case LG_CCSDS_APID_COMMAND:
        if (view.header.type == CCSDS_PACKET_TYPE_COMMAND) break;
    default:
        fprintf(stderr, "Dropping unsupported CCSDS APID/type (%u/%u)\n",
                (unsigned int)view.header.apid, (unsigned int)view.header.type);
        return CCSDS_OK;
    }
    if (view.header.apid == LG_CCSDS_APID_SSDV) {
        receive_ssdv(context, view.data, view.data_length);
        return CCSDS_OK;
    }

    rv = nng_msg_alloc(&message, view.data_length);
    if (rv == 0) {
        memcpy(nng_msg_body(message), view.data, view.data_length);
        rv = nng_sendmsg(context->publisher, message, NNG_FLAG_NONBLOCK);
        if (rv != 0) nng_msg_free(message);
    }
    if (rv != 0) fprintf(stderr, "Publishing received payload failed: %s\n", nng_strerror(rv));

    return CCSDS_OK;
}

static int send_command(nng_msg *message, uint16_t *sequence) {
    uint8_t packet[CCSDS_SPACE_PACKET_MAX_TOTAL_SIZE];
    size_t packet_length;
    ccsds_space_packet_header_t header = {
        .type = CCSDS_PACKET_TYPE_COMMAND,
        .has_secondary_header = false,
        .apid = LG_CCSDS_APID_COMMAND,
        .sequence_flags = CCSDS_SEQUENCE_UNSEGMENTED,
        .sequence_count = *sequence
    };
    ccsds_status_t status;
    int rv;

    if (!valid_payload(nng_msg_body(message), nng_msg_len(message))) {
        fprintf(stderr, "Dropping NNG command with invalid payload length: %zu\n", nng_msg_len(message));
        return 0;
    }
    status = ccsds_space_packet_build(&header, nng_msg_body(message), nng_msg_len(message),
                                      packet, sizeof(packet), &packet_length);
    if (status != CCSDS_OK) {
        fprintf(stderr, "Unable to build CCSDS command: %s\n", ccsds_status_string(status));
        return -1;
    }
    rv = radio_send(packet, packet_length);
    if (rv < 0) {
        fprintf(stderr, "Radio send failed (%d); command transmission is uncertain\n", rv);
        return -1;
    }
    *sequence = (uint16_t)((*sequence + 1U) & CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT);
    return 0;
}

int main(void) {
    uint8_t radio_buffer[RADIO_BUFFER_SIZE];
    uint8_t packet_storage[CCSDS_SPACE_PACKET_MAX_TOTAL_SIZE];
    ccsds_stream_parser_t parser;
    receive_context_t context = {.publisher = NNG_SOCKET_INITIALIZER};
    nng_socket commands = NNG_SOCKET_INITIALIZER;
    int publisher_open = 0, commands_open = 0;
    int result = EXIT_FAILURE, rv;
    uint16_t sequence = 0;
    nng_time radio_retry_at = 0;
    ccsds_status_t status;

    status = ccsds_stream_parser_init(&parser, packet_storage, sizeof(packet_storage));
    if (status != CCSDS_OK) {
        fprintf(stderr, "Unable to initialize CCSDS parser: %s\n", ccsds_status_string(status));
        goto cleanup;
    }
    if ((rv = nng_pub0_open(&context.publisher)) != 0) goto nng_error;
    publisher_open = 1;
    if ((rv = nng_listen(context.publisher, GROUNDSTATION_NNG_URL, NULL, 0)) != 0) goto nng_error;
    if ((rv = nng_pull0_open(&commands)) != 0) goto nng_error;
    commands_open = 1;
    if ((rv = nng_socket_set_size(commands, NNG_OPT_RECVMAXSZ, GROUNDSTATION_PACKET_DATA_CAPACITY)) != 0)
        goto nng_error;
    if ((rv = nng_listen(commands, GROUNDSTATION_COMMAND_NNG_URL, NULL, 0)) != 0) goto nng_error;

    (void)signal(SIGINT, request_shutdown);
    (void)signal(SIGTERM, request_shutdown);
    fprintf(stderr, "Received payloads: SUB %s\nCommands: PUSH %s\n",
            GROUNDSTATION_NNG_URL, GROUNDSTATION_COMMAND_NNG_URL);
    fprintf(stderr, "SSDV images are saved as ssdv-*.jpg in the working directory\n");

    while (keep_running) {
        int did_work = 0;
        nng_msg *message = NULL;

        if (nng_clock() >= radio_retry_at) {
            int received = radio_receive(radio_buffer, (uint16_t)sizeof(radio_buffer));
            if (received < 0 || (size_t)received > sizeof(radio_buffer)) {
                fprintf(stderr, "Radio receive failed or returned invalid length (%d)\n", received);
                ccsds_stream_parser_reset(&parser);
                radio_retry_at = nng_clock() + 250U;
            } else if (received > 0) {
                size_t delivered = 0U;
                did_work = 1;
                status = ccsds_stream_parser_feed(&parser, radio_buffer, (size_t)received,
                                                  receive_packet, &context, &delivered);
                if (status != CCSDS_OK) {
                    fprintf(stderr, "CCSDS stream parse failed: %s\n", ccsds_status_string(status));
                    ccsds_stream_parser_reset(&parser);
                }
            }
        }

        rv = nng_recvmsg(commands, &message, NNG_FLAG_NONBLOCK);
        if (rv == 0) {
            int sent = send_command(message, &sequence);
            nng_msg_free(message);
            if (sent != 0) goto cleanup;
            did_work = 1;
        } else if (rv != NNG_EAGAIN) {
            goto nng_error;
        }
        if (!did_work) nng_msleep(10);
    }
    result = EXIT_SUCCESS;
    goto cleanup;

nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(rv));
cleanup:
    if (commands_open) (void)nng_close(commands);
    if (publisher_open) (void)nng_close(context.publisher);
    finish_image(&context, 1);
    reset_image(&context);
    return result;
}
