// This is slopity slop and a sim stub. Please delete when possible.
#include <stdint.h>
#include <string.h>

#define BUF_SIZE 256

static uint8_t radio_buffer[BUF_SIZE];

void radio_send(const uint8_t *data, uint16_t len) {
    if (len > BUF_SIZE) len = BUF_SIZE;
    memcpy(radio_buffer, data, len);
}

void radio_recv(uint8_t *data, uint16_t len) {
    if (len > BUF_SIZE) len = BUF_SIZE;
    memcpy(data, radio_buffer, len);
}
