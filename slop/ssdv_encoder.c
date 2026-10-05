#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include "ssdv.h"

static void usage(FILE *stream)
{
	fprintf(stream,
		"Usage: GroundStationSSDVEncoder [-e|-d] [options] [input] [output]\n"
		"  -e              Encode JPEG to SSDV packets\n"
		"  -d              Decode SSDV packets to JPEG\n"
		"  -c callsign     Up to 6 characters: A-Z, 0-9 or space\n"
		"  -i id           Image ID, 0-255 (default 0)\n"
		"  -q quality      Encoding quality, 0-7 (default 4)\n"
		"  -n              Encode without Reed-Solomon FEC\n"
		"  -l length       Packet length, 53-256 (default 256)\n"
		"  -t percentage   Drop 0-100 percent of packets while decoding\n"
		"  -v              Print decoded packet details\n"
		"  -h              Show this help\n"
		"Missing filenames or '-' use stdin/stdout. Specify custom packet lengths\n"
		"for both encoding and decoding. Encoding requires an 8-bit baseline JPEG\n"
		"with dimensions divisible by 16 and at most 65535 MCU blocks.\n"
		"Each image must fit within 65536 packets; reduce size or quality if needed.\n");
}

static int parse_number(const char *text, int minimum, int maximum, int *value)
{
	char *end;
	long number;

	errno = 0;
	number = strtol(text, &end, 10);
	if(errno || end == text || *end || number < minimum || number > maximum)
	{
		fprintf(stderr, "Invalid value '%s': expected %d-%d\n", text, minimum, maximum);
		return EXIT_FAILURE;
	}
	*value = (int) number;
	return EXIT_SUCCESS;
}

static int encode_image(FILE *fin, FILE *fout, uint8_t type, char *callsign,
	uint8_t image_id, int8_t quality, int pkt_length)
{
	ssdv_t ssdv;
	uint8_t pkt[SSDV_PKT_SIZE], input[128];
	size_t packets = 0;
	int result;

	if(ssdv_enc_init(&ssdv, type, callsign, image_id, quality, pkt_length) != SSDV_OK ||
	   ssdv_enc_set_buffer(&ssdv, pkt) != SSDV_OK)
		return EXIT_FAILURE;

	for(;;)
	{
		result = ssdv_enc_get_packet(&ssdv);
		if(result == SSDV_FEED_ME)
		{
			size_t length = fread(input, 1, sizeof(input), fin);
			if(length == 0)
			{
				if(ferror(fin)) perror("Reading JPEG input");
				else fprintf(stderr, "Premature end of JPEG input; expected a complete baseline JPEG\n");
				return EXIT_FAILURE;
			}
			if(ssdv_enc_feed(&ssdv, input, length) != SSDV_OK) return EXIT_FAILURE;
		}
		else if(result == SSDV_OK)
		{
			if(packets > UINT16_MAX)
			{
				fprintf(stderr, "Image exceeds the 65536 packet limit; reduce image size or quality\n");
				return EXIT_FAILURE;
			}
			if(fwrite(pkt, 1, (size_t) pkt_length, fout) != (size_t) pkt_length)
			{
				perror("Writing SSDV output");
				return EXIT_FAILURE;
			}
			packets++;
		}
		else if(result == SSDV_EOI)
		{
			if(packets == 0)
			{
				fprintf(stderr, "JPEG input produced no SSDV packets\n");
				return EXIT_FAILURE;
			}
			fprintf(stderr, "Wrote %zu packets\n", packets);
			return EXIT_SUCCESS;
		}
		else
		{
			fprintf(stderr, "ssdv_enc_get_packet failed: %d\n", result);
			return EXIT_FAILURE;
		}
	}
}

static int decode_image(FILE *fin, FILE *fout, int pkt_length, int verbose, int droptest)
{
	ssdv_t ssdv;
	ssdv_packet_info_t image = {0};
	uint8_t pkt[SSDV_PKT_SIZE], *jpeg = NULL;
	size_t jpeg_length = 0, packets = 0;
	int result, errors, status = EXIT_FAILURE, complete = 0;

	if(ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK) return EXIT_FAILURE;
	if(droptest) fprintf(stderr, "*** Drop test enabled: %d%% ***\n", droptest);

	while(fread(pkt, 1, (size_t) pkt_length, fin) == (size_t) pkt_length)
	{
		ssdv_packet_info_t p;
		size_t skipped = 0;

		/* Slide over noise until a complete, validated packet is found. */
		while((result = ssdv_dec_is_packet(pkt, pkt_length, &errors)) != SSDV_OK)
		{
			memmove(pkt, pkt + 1, pkt_length - 1);
			if(fread(pkt + pkt_length - 1, 1, 1, fin) != 1) break;
			skipped++;
		}
		if(result != SSDV_OK) break;
		if(droptest && (double) rand() / ((double) RAND_MAX + 1.0) * 100 < droptest) continue;

		ssdv_dec_header(&p, pkt);
		/* A stream starting mid-MCU cannot initialize the decoder yet. */
		if(jpeg == NULL && p.packet_id != 0 && p.mcu_id == UINT16_MAX) continue;
		if(jpeg == NULL)
		{
			size_t mcu_count = (size_t) (p.width / 16) * (p.height / 16);
			size_t blocks = p.mcu_mode == 0 ? 6 : p.mcu_mode == 3 ? 3 : 4;
			if(p.mcu_mode == 1 || p.mcu_mode == 2) mcu_count *= 2;
			else if(p.mcu_mode == 3) mcu_count *= 4;
			if(mcu_count > UINT16_MAX)
			{
				fprintf(stderr, "SSDV image exceeds the 65535 MCU block limit\n");
				goto cleanup;
			}
			/* Reserve a worst-case JPEG size before feeding the library: it does
			 * not recover from a full output buffer. Each block has 64 coefficients,
			 * at most 32 bits each, with up to twice the bytes for JPEG stuffing. */
			jpeg_length = 1024 + mcu_count * blocks * 512;
			jpeg = malloc(jpeg_length);
			if(jpeg == NULL)
			{
				perror("Allocating JPEG buffer");
				goto cleanup;
			}
			/* Avoid subtracting null pointers in the library's buffer setter. */
			ssdv.out = ssdv.outp = jpeg;
			if(ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length) != SSDV_OK) goto cleanup;
			image = p;
		}
		else if(p.callsign != image.callsign || p.image_id != image.image_id ||
		        p.width != image.width || p.height != image.height ||
		        p.type != image.type || p.quality != image.quality || p.mcu_mode != image.mcu_mode)
		{
			fprintf(stderr, "Input contains packets from different images\n");
			goto cleanup;
		}

		if(verbose)
		{
			if(skipped) fprintf(stderr, "Skipped %zu bytes\n", skipped);
			fprintf(stderr, "Packet %u: callsign '%s', image %u, %ux%u, %d errors corrected\n",
				p.packet_id, p.callsign_s, p.image_id, p.width, p.height, errors);
		}

		result = ssdv_dec_feed(&ssdv, pkt);
		if(result != SSDV_OK && result != SSDV_FEED_ME)
		{
			fprintf(stderr, "ssdv_dec_feed failed: %d\n", result);
			goto cleanup;
		}
		/* The library initializes JPEG headers whenever its counter is zero;
		 * feeding another packet after a wrap would overwrite its tables. */
		if(result == SSDV_FEED_ME && ssdv.packet_id == 0)
		{
			if(p.packet_id == UINT16_MAX)
				fprintf(stderr, "Image exceeds the 65536 packet limit; reduce image size or quality\n");
			else fprintf(stderr, "Could not advance decoder packet sequence\n");
			goto cleanup;
		}
		if(ssdv.out_len == 0)
		{
			fprintf(stderr, "JPEG output buffer exhausted\n");
			goto cleanup;
		}
		packets++;
		if(result == SSDV_OK)
		{
			complete = 1;
			break;
		}
	}

	if(ferror(fin))
	{
		perror("Reading SSDV input");
		goto cleanup;
	}
	if(packets == 0)
	{
		fprintf(stderr, "No valid SSDV image packets found\n");
		goto cleanup;
	}
	if(!complete) fprintf(stderr, "Image incomplete; missing blocks will be filled\n");
	if(ssdv_dec_get_jpeg(&ssdv, &jpeg, &jpeg_length) != SSDV_OK || ssdv.out_len == 0)
	{
		fprintf(stderr, "Could not finalize decoded JPEG\n");
		goto cleanup;
	}
	if(fwrite(jpeg, 1, jpeg_length, fout) != jpeg_length)
	{
		perror("Writing JPEG output");
		goto cleanup;
	}
	fprintf(stderr, "Read %zu packets\n", packets);
	status = EXIT_SUCCESS;

cleanup:
	free(jpeg);
	return status;
}

int main(int argc, char *argv[])
{
	FILE *fin = stdin, *fout = stdout;
	char callsign[SSDV_MAX_CALLSIGN + 1] = "";
	int encode = -1, type = SSDV_TYPE_NORMAL, image_id = 0, quality = 4;
	int pkt_length = SSDV_PKT_SIZE, droptest = 0, verbose = 0;
	int option, status = EXIT_FAILURE;

	opterr = 0;
	while((option = getopt(argc, argv, "ednc:i:q:l:t:vh")) != -1)
	{
		switch(option)
		{
		case 'e':
		case 'd':
			if(encode != -1 && encode != (option == 'e'))
			{
				fprintf(stderr, "Choose exactly one mode: -e or -d\n");
				return EXIT_FAILURE;
			}
			encode = option == 'e';
			break;
		case 'n': type = SSDV_TYPE_NOFEC; break;
		case 'c':
			if(strlen(optarg) > SSDV_MAX_CALLSIGN ||
			   strspn(optarg, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ") != strlen(optarg))
			{
				fprintf(stderr, "Callsign must contain up to 6 characters: A-Z, 0-9 or space\n");
				return EXIT_FAILURE;
			}
			strcpy(callsign, optarg);
			break;
		case 'i': if(parse_number(optarg, 0, 255, &image_id)) return EXIT_FAILURE; break;
		case 'q': if(parse_number(optarg, 0, 7, &quality)) return EXIT_FAILURE; break;
		case 'l':
			/* Packet validation also attempts normal FEC, even for no-FEC data. */
			if(parse_number(optarg, SSDV_PKT_SIZE_HEADER + SSDV_PKT_SIZE_CRC +
				SSDV_PKT_SIZE_RSCODES + 2, SSDV_PKT_SIZE, &pkt_length)) return EXIT_FAILURE;
			break;
		case 't': if(parse_number(optarg, 0, 100, &droptest)) return EXIT_FAILURE; break;
		case 'v': verbose = 1; break;
		case 'h': usage(stdout); return EXIT_SUCCESS;
		default: usage(stderr); return EXIT_FAILURE;
		}
	}
	if(encode == -1 || argc - optind > 2)
	{
		usage(stderr);
		return EXIT_FAILURE;
	}
	if(optind < argc && strcmp(argv[optind], "-"))
	{
		fin = fopen(argv[optind], "rb");
		if(fin == NULL)
		{
			perror(argv[optind]);
			return EXIT_FAILURE;
		}
	}
	if(optind + 1 < argc && strcmp(argv[optind + 1], "-"))
	{
		struct stat input_stat, output_stat;
		if(fstat(fileno(fin), &input_stat) == 0 && stat(argv[optind + 1], &output_stat) == 0 &&
		   input_stat.st_dev == output_stat.st_dev && input_stat.st_ino == output_stat.st_ino)
		{
			fprintf(stderr, "Input and output must be different files\n");
			goto cleanup;
		}
		fout = fopen(argv[optind + 1], "wb");
		if(fout == NULL)
		{
			perror(argv[optind + 1]);
			goto cleanup;
		}
	}

	status = encode ? encode_image(fin, fout, (uint8_t) type, callsign,
		(uint8_t) image_id, (int8_t) quality, pkt_length) :
		decode_image(fin, fout, pkt_length, verbose, droptest);

cleanup:
	if(fin != stdin && fclose(fin) != 0)
	{
		perror("Closing input");
		status = EXIT_FAILURE;
	}
	if(fout != NULL && (fout == stdout ? fflush(fout) : fclose(fout)) != 0)
	{
		perror("Closing output");
		status = EXIT_FAILURE;
	}
	return status;
}
