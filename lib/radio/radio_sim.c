#include <stdio.h>

#include "radio.h"

/* ANDREW IS WORKING ON REAL RADIO */
/*
The entire radio code will need to be revised once we get a Sat Nogs COMM Board, but for know we treat the E22 radio as part of the board itself.
*/

int radio_send(const uint8_t *data, size_t length) {
    (void)data;
    printf("[RADIO] (mock) would transmit %zu bytes to satellite\n", length);
    fflush(stdout);
    return 0;
}

int radio_receive(uint8_t *buffer, uint16_t max_length) {
    (void)buffer;
    (void)max_length;
    return 0;
}
