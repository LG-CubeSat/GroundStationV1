#ifndef SIMPL_SSDV_H
#define SIMPL_SSDV_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "SSDV/ssdv.h"

// Sloppity-slop header to simplify SSDV development

/*
 * Single-header, in-memory wrapper around the bundled SSDV engine.
 * Link against SSDV (or link the CMake target simpl_SSDV).
 *
 * Both functions take immutable bytes and their length. On success, the caller
 * owns the returned data and must free() it. On failure, data is NULL, length is
 * zero, and errno is EINVAL (invalid/unsupported input), ENOMEM, or ERANGE
 * (an SSDV format limit was exceeded). Only inspect errno on failure.
 *
 * Encoding expects compressed baseline JPEG bytes, not raw pixels. Defaults:
 * callsign SIMPLE, image ID 0, quality 4, FEC, 256-byte packets.
 * Decoding accepts 256-byte packets with or without FEC, skips noise/corrupt
 * packets, and returns the first image. Packets must be in transmission order.
 * Missing packets can produce a recovered JPEG with missing blocks filled.
 * The underlying SSDV engine may print diagnostics to stderr.
 */
typedef struct {
	uint8_t *data;
	size_t length;
} simpl_ssdv_buffer;

static inline simpl_ssdv_buffer simpl_ssdv_encode(const uint8_t *jpeg, size_t jpeg_length) {
	simpl_ssdv_buffer result = {NULL, 0};
	ssdv_t ssdv;
	uint8_t packet[SSDV_PKT_SIZE], chunk[128], *output = NULL;
	char callsign[] = "SIMPLE";
	size_t position = 0, length = 0, capacity = 0;
	const size_t limit = ((size_t) UINT16_MAX + 1) * SSDV_PKT_SIZE;
	int status, error = EINVAL;

	if(jpeg == NULL || jpeg_length < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
		goto failure;
	if(ssdv_enc_init(&ssdv, SSDV_TYPE_NORMAL, callsign, 0, 4, SSDV_PKT_SIZE) != SSDV_OK ||
	   ssdv_enc_set_buffer(&ssdv, packet) != SSDV_OK)
		goto failure;

	for(;;) {
		status = ssdv_enc_get_packet(&ssdv);
		if(status == SSDV_FEED_ME) {
			size_t count = jpeg_length - position;
			if(count == 0) goto failure;
			if(count > sizeof(chunk)) count = sizeof(chunk);
			memcpy(chunk, jpeg + position, count);
			position += count;
			if(ssdv_enc_feed(&ssdv, chunk, count) != SSDV_OK) goto failure;
		}
		else if(status == SSDV_OK) {
			if(length == limit) {
				error = ERANGE;
				goto failure;
			}
			if(length + SSDV_PKT_SIZE > capacity) {
				uint8_t *grown;
				size_t next = capacity ? capacity * 2 : 4096;
				if(next > limit) next = limit;
				grown = (uint8_t *) realloc(output, next);
				if(grown == NULL) {
					error = ENOMEM;
					goto failure;
				}
				output = grown;
				capacity = next;
			}
			memcpy(output + length, packet, SSDV_PKT_SIZE);
			length += SSDV_PKT_SIZE;
		}
		else if(status == SSDV_EOI) {
			if(length == 0) goto failure;
			result.data = output;
			result.length = length;
			return result;
		}
		else goto failure;
	}

failure:
	free(output);
	errno = error;
	return result;
}

static inline simpl_ssdv_buffer simpl_ssdv_decode(const uint8_t *packets, size_t packets_length) {
	simpl_ssdv_buffer result = {NULL, 0};
	ssdv_t ssdv;
	ssdv_packet_info_t image;
	uint8_t packet[SSDV_PKT_SIZE], *jpeg = NULL;
	size_t position = 0, jpeg_length = 0, fed = 0;
	int status, error = EINVAL;

	if(packets == NULL || packets_length < SSDV_PKT_SIZE) goto failure;
	if(ssdv_dec_init(&ssdv, SSDV_PKT_SIZE) != SSDV_OK) goto failure;

	while(packets_length - position >= SSDV_PKT_SIZE) {
		ssdv_packet_info_t p;

		/* Validation can repair the packet, so work on a copy of the input. */
		memcpy(packet, packets + position, SSDV_PKT_SIZE);
		if(ssdv_dec_is_packet(packet, SSDV_PKT_SIZE, NULL) != SSDV_OK) {
			position++;
			continue;
		}
		position += SSDV_PKT_SIZE;
		ssdv_dec_header(&p, packet);
		if(jpeg == NULL && p.packet_id != 0 && p.mcu_id == UINT16_MAX) continue;

		if(jpeg == NULL) {
			size_t mcus = (size_t) (p.width / 16) * (p.height / 16);
			size_t blocks = p.mcu_mode == 0 ? 6 : p.mcu_mode == 3 ? 3 : 4;
			if(p.mcu_mode == 1 || p.mcu_mode == 2) mcus *= 2;
			else if(p.mcu_mode == 3) mcus *= 4;
			if(mcus > UINT16_MAX) {
				error = ERANGE;
				goto failure;
			}
			/* The engine cannot grow a full output buffer. Reserve a worst-case
			 * JPEG: 64 coefficients * 32 bits * double byte stuffing per block,
			 * plus room for headers and the final marker. */
			jpeg_length = 1024 + mcus * blocks * 512;
			jpeg = (uint8_t *) malloc(jpeg_length);
			if(jpeg == NULL) {
				error = ENOMEM;
				goto failure;
			}
			ssdv.out = ssdv.outp = jpeg;
			if(ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length) != SSDV_OK) goto failure;
			image = p;
		}
		else if(p.callsign != image.callsign || p.image_id != image.image_id ||
		        p.width != image.width || p.height != image.height ||
		        p.type != image.type || p.quality != image.quality || p.mcu_mode != image.mcu_mode)
			goto failure;

		/* Ignore duplicates/late packets without changing the decoder state. */
		if(p.packet_id < ssdv.packet_id) continue;
		status = ssdv_dec_feed(&ssdv, packet);
		if(status != SSDV_OK && status != SSDV_FEED_ME) goto failure;
		if(status == SSDV_FEED_ME && ssdv.packet_id == 0) {
			if(p.packet_id == UINT16_MAX) error = ERANGE;
			goto failure;
		}
		if(ssdv.out_len == 0) {
			error = ERANGE;
			goto failure;
		}
		fed++;
		if(status == SSDV_OK) break;
	}

	if(fed == 0 || ssdv_dec_get_jpeg(&ssdv, &jpeg, &jpeg_length) != SSDV_OK) goto failure;
	if(ssdv.out_len == 0) {
		error = ERANGE;
		goto failure;
	}
	result.data = jpeg;
	result.length = jpeg_length;
	return result;

failure:
	free(jpeg);
	errno = error;
	return result;
}

#endif
