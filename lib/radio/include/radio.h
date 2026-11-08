#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

#define RADIO_MAX_PACKET_SIZE 240 // e22 max bytes per over-the-air packet
#define RADIO_FRAME_OVERHEAD 26   // ccsds header 6 + lg profile header 16 + crc 4
#define RADIO_MAX_PAYLOAD (RADIO_MAX_PACKET_SIZE - RADIO_FRAME_OVERHEAD) // 214

/*
every packet on the air is one ccsds space packet (lg profile, see shared/ccsds).
callers hand over plain bytes, the radio does the framing.
send calls queue and return right away, a tx thread does the actual sending.
*/

/* telemetry. split into <= RADIO_MAX_PAYLOAD pieces. 0 = queued, -1 = error / no room */
int radio_send(const uint8_t *data, size_t length);

/* whole ssdv packets back to back. length must be a multiple of 214, one ssdv packet per radio packet */
int radio_send_ssdv(const uint8_t *data, size_t length);

/* waits for one valid packet and copies out its payload.
   returns payload bytes, 0 = timed out, -1 = error or payload bigger than max_length */
int radio_receive(uint8_t *buffer, uint16_t max_length);

/* arq: send a recently sent packet again, byte for byte, same sequence count.
   apid is the ccsds apid (e.g. LG_CCSDS_APID_SSDV). 0 = queued, -1 = too old or never sent */
int radio_resend(uint16_t apid, uint16_t sequence_count);

typedef struct {
    uint32_t packets_sent;     // made it to the radio, resends included
    uint32_t packets_resent;   // arq resends
    uint32_t packets_retried;  // extra send attempts after a failure
    uint32_t packets_dropped;  // gave up after all retries
    uint32_t queue_rejects;    // radio_send calls refused, buffer full or bad args
    uint32_t queue_high_water; // most bytes ever waiting in the tx buffer
    uint32_t rx_packets;       // valid packets received
    uint32_t rx_invalid;       // received packets that failed the ccsds checks
    uint32_t rx_bytes_skipped; // garbage bytes thrown away while finding the next packet
} radio_stats_t;

void radio_get_stats(radio_stats_t *stats);

#endif
