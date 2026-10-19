#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "wire format is little-endian"
#elif !defined(__BYTE_ORDER__) && !defined(_MSC_VER)
#error "unknown compiler: can't confirm endianness"
#endif

#pragma pack(push, 1)
typedef struct {
    int32_t x;
    int32_t y;
} Point;

typedef struct {
    int32_t temp;
    char text[8];
} TempData;
#pragma pack(pop)

_Static_assert(sizeof(Point) == 8, "Point must be 8 bytes");
_Static_assert(sizeof(TempData) == 12, "TempData must be 12 bytes");

void serialize(const void *data, char *buffer, size_t size) {
    memcpy(buffer, data, size);
}

void deserialize(const char *buffer, void *data, size_t size) {
    memcpy(data, buffer, size);
}
