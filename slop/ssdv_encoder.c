#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "ssdv.h"

#define INPUT_PATH "photo.jpeg"
#define OUTPUT_PATH "photo.ssdv"
#define INPUT_CHUNK_SIZE 4096

int main(void)
{
	FILE *input_file = fopen(INPUT_PATH, "rb");
	FILE *output_file = NULL;
	ssdv_t encoder;
	uint8_t packet[SSDV_PKT_SIZE];
	uint8_t input_chunk[INPUT_CHUNK_SIZE];
	char result;

	if(input_file == NULL)
	{
		perror("Could not open " INPUT_PATH);
		return EXIT_FAILURE;
	}

	output_file = fopen(OUTPUT_PATH, "wb");
	if(output_file == NULL)
	{
		perror("Could not open " OUTPUT_PATH);
		fclose(input_file);
		return EXIT_FAILURE;
	}

	if(ssdv_enc_init(&encoder, SSDV_TYPE_NORMAL, "SLOP01", 1, 4,
	                 SSDV_PKT_SIZE) != SSDV_OK)
	{
		fprintf(stderr, "Could not initialize SSDV encoder\n");
		fclose(output_file);
		fclose(input_file);
		return EXIT_FAILURE;
	}

	ssdv_enc_set_buffer(&encoder, packet);

	for(;;)
	{
		result = ssdv_enc_get_packet(&encoder);

		if(result == SSDV_FEED_ME)
		{
			size_t bytes_read = fread(input_chunk, 1, sizeof(input_chunk), input_file);

			if(bytes_read == 0)
			{
				if(ferror(input_file))
				{
					perror("Could not read " INPUT_PATH);
				}
				else
				{
					fprintf(stderr, "Unexpected end of JPEG before its EOI marker\n");
				}
				fclose(output_file);
				fclose(input_file);
				return EXIT_FAILURE;
			}

			ssdv_enc_feed(&encoder, input_chunk, bytes_read);
			continue;
		}

		if(result == SSDV_OK)
		{
			if(fwrite(packet, 1, sizeof(packet), output_file) != sizeof(packet))
			{
				perror("Could not write " OUTPUT_PATH);
				fclose(output_file);
				fclose(input_file);
				return EXIT_FAILURE;
			}
			continue;
		}

		if(result == SSDV_EOI)
		{
			break;
		}

		fprintf(stderr, "SSDV encoding failed: %d\n", result);
		fclose(output_file);
		fclose(input_file);
		return EXIT_FAILURE;
	}

	if(fclose(output_file) != 0)
	{
		perror("Could not close " OUTPUT_PATH);
		fclose(input_file);
		return EXIT_FAILURE;
	}
	fclose(input_file);

	return EXIT_SUCCESS;
}
