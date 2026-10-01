#include "ccsds/crc32.h"

uint32_t ccsds_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t i;

    if (data == NULL && length != 0U) {
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        unsigned int bit;

        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}
