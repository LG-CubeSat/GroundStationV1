#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

int radio_send(const uint8_t *data, size_t length);

/* Returns received byte count, 0 when no data is available, or a negative error. */
int radio_receive(uint8_t *buffer, uint16_t max_length);

#endif
