#include "serial.h"

#include <string.h>

serial_status_t serialize(const void *data, size_t size, void *buffer, size_t capacity, size_t *written) {
    if (written == NULL) {
        return SERIAL_ERROR_NULL;
    }
    *written = 0U;
    if (data == NULL || buffer == NULL) {
        return SERIAL_ERROR_NULL;
    }
    if (size == 0U) {
        return SERIAL_ERROR_SIZE;
    }
    if (capacity < size) {
        return SERIAL_ERROR_BUFFER_TOO_SMALL;
    }
    memmove(buffer, data, size);
    *written = size;
    return SERIAL_OK;
}

serial_status_t deserialize(const void *buffer, size_t length, void *data, size_t size) {
    if (buffer == NULL || data == NULL) {
        return SERIAL_ERROR_NULL;
    }
    if (size == 0U || length != size) {
        return SERIAL_ERROR_SIZE;
    }
    memmove(data, buffer, size);
    return SERIAL_OK;
}

const char *serial_status_string(serial_status_t status) {
    switch (status) {
    case SERIAL_OK:
        return "success";
    case SERIAL_ERROR_NULL:
        return "null pointer";
    case SERIAL_ERROR_SIZE:
        return "invalid object size or message length";
    case SERIAL_ERROR_BUFFER_TOO_SMALL:
        return "output buffer too small";
    default:
        return "unknown serialization error";
    }
}
