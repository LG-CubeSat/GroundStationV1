#ifndef GROUNDSTATION_SERIAL_H
#define GROUNDSTATION_SERIAL_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Ground station serialization requires a little-endian target"
#endif
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64) || \
                            defined(_M_ARM) || defined(_M_ARM64))
#else
#error "Cannot verify target endianness for ground station serialization"
#endif

_Static_assert(CHAR_BIT == 8, "Serialization requires 8-bit bytes");
_Static_assert(sizeof(uint32_t) == 4, "Serialization requires 4-byte uint32_t");

typedef enum {
    SERIAL_OK = 0,
    SERIAL_ERROR_NULL = -1,
    SERIAL_ERROR_SIZE = -2,
    SERIAL_ERROR_BUFFER_TOO_SMALL = -3
} serial_status_t;

serial_status_t serialize(const void *data, size_t size, void *buffer, size_t capacity, size_t *written);

serial_status_t deserialize(const void *buffer, size_t length, void *data, size_t size);

const char *serial_status_string(serial_status_t status);

#endif
