#ifndef CCSDS_CRC32_H
#define CCSDS_CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-32/ISO-HDLC (also called the Ethernet CRC-32):
 * polynomial 0x04C11DB7, reflected input/output, init/xorout 0xFFFFFFFF.
 * The LG-CubeSat profile uses it as an end-to-end packet integrity check.
 */
uint32_t ccsds_crc32(const uint8_t *data, size_t length);

#endif
