#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "simpl_SSDV.h"

#define INPUT_SSDV_PATH "../build/test.ssdv"
#define OUTPUT_IMAGE_PATH "decoded.jpg"

static simpl_ssdv_buffer load_failure(FILE *file, uint8_t *data) {
    simpl_ssdv_buffer packets = {NULL, 0};
    int saved_errno = errno;

    fclose(file);
    free(data);
    errno = saved_errno;
    return packets;
}

static simpl_ssdv_buffer load_packets(const char *path) {
    simpl_ssdv_buffer packets = {NULL, 0};
    FILE *file = fopen(path, "rb");
    uint8_t *data = NULL;
    long length;
    int saved_errno;

    if (file == NULL) return packets;
    if (fseek(file, 0, SEEK_END) != 0) return load_failure(file, data);
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) return load_failure(file, data);

    if (length == 0) {
        errno = EINVAL;
        return load_failure(file, data);
    }
data = malloc((size_t) length);

    if (data == NULL) {
        errno = ENOMEM;
        return load_failure(file, data);
    }

    if (fread(data, 1, (size_t) length, file) != (size_t) length) {
        if (!ferror(file)) errno = EIO;
        return load_failure(file, data);
    }

    if (fclose(file) != 0) {
        saved_errno = errno;
        free(data);
        errno = saved_errno;
        return packets;
    }

    packets.data = data;
    packets.length = (size_t) length;
    return packets;
}

static int save_image(const char *path, const uint8_t *data, size_t length) {
    FILE *file = fopen(path, "wb");
    int saved_errno;

    if (file == NULL) return -1;
    if (fwrite(data, 1, length, file) != length) {
        saved_errno = errno ? errno : EIO;
        fclose(file);
        errno = saved_errno;
        return -1;
    }
    return fclose(file);
}

int main(void) {
    simpl_ssdv_buffer packets = load_packets(INPUT_SSDV_PATH);
    simpl_ssdv_buffer image;

    if (packets.data == NULL) {
        perror(INPUT_SSDV_PATH);
        return EXIT_FAILURE;
    }

    image = simpl_ssdv_decode(packets.data, packets.length);

    if (image.data == NULL) {
        perror("Decoding SSDV");
        free(packets.data);
        return EXIT_FAILURE;
    }

    free(packets.data);

    if (save_image(OUTPUT_IMAGE_PATH, image.data, image.length) != 0) {
        perror(OUTPUT_IMAGE_PATH);
        free(image.data);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Wrote %zu bytes to %s\n", image.length, OUTPUT_IMAGE_PATH);
    free(image.data);
    return EXIT_SUCCESS;
}
