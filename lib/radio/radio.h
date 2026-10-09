#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

int radio_send(const uint8_t *data, size_t length);

int radio_receive(uint8_t *buffer, uint16_t max_length);

#endif