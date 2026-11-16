#ifndef GS_RADIO_TRANSPORT_H
#define GS_RADIO_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* GS_SIM_RF=1 connects GSMain directly to flight's UDP radio simulator. */
int gs_radio_init(void);
int gs_radio_receive_packet(uint8_t *buffer, uint16_t max_length);
int gs_radio_send_command(const uint8_t *payload, size_t length);
void gs_radio_close(void);

#endif
