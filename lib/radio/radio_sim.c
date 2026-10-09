/* ANDREW IS WORKING ON REAL RADIO */
#include "radio.h"
#include <stdio.h>
#include <stdint.h>

/*
The entire radio code will need to be revised once we get a Sat Nogs COMM Board, but for know we treat the E22 radio as part of the board itself.
*/

int radio_send(const uint8_t *data, size_t length)
{
    (void)data;
    printf("[RADIO] (mock) would transmit %zu bytes to ground\n", length);
    fflush(stdout);
    return 0;
}

int radio_receive(uint8_t *buffer, uint16_t max_length) {
    (void)*buffer;
    printf("[RADIO] (mock) would receive up to %zu bytes from ground\n", (size_t) max_length);
    fflush(stdout);
    return 0;
}