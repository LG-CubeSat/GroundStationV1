#ifndef PACKET_POLICE_H
#define PACKET_POLICE_H

#include <stdbool.h>
#include <stdint.h>

#include "ccsds/profile.h"
#include "ccsds/status.h"
#include "ssdv.h"

/*
packet police: decides if a received packet is trusted before anything uses it.
runs after lg_ccsds_profile_parse. the parse checks one packet on its own
(header, crc, content type). police adds the stuff that needs history:
duplicates, missing packets, ssdv contents, and counts for logs / ui.
*/

#define PACKET_POLICE_CONTENT_TYPES 4 // raw tlm, compressed tlm, ssdv, command
#define PACKET_POLICE_RECENT 64       // how many recent sequence counts to remember per type

typedef enum {
    PACKET_POLICE_ACCEPT = 0,
    PACKET_POLICE_REJECT_MALFORMED,  // ccsds parse failed (bad header, length, format)
    PACKET_POLICE_REJECT_CRC,        // bytes got corrupted in the air
    PACKET_POLICE_REJECT_DUPLICATE,  // already got this sequence count
    PACKET_POLICE_REJECT_BAD_SSDV,   // ssdv payload isn't a valid ssdv packet
    PACKET_POLICE_REJECT_CALLSIGN,   // ssdv from a different callsign, not our satellite
    PACKET_POLICE_VERDICT_COUNT
} packet_police_verdict_t;

typedef struct {
    uint32_t received;
    uint32_t accepted;
    uint32_t rejected[PACKET_POLICE_VERDICT_COUNT]; // indexed by verdict
    uint32_t missing; // packets we never got, from gaps in sequence count
    uint32_t resyncs; // sequence count jumped backwards (satellite rebooted)
} packet_police_stats_t;

typedef struct {
    bool seen_any;
    uint16_t last;
    uint16_t recent[PACKET_POLICE_RECENT];
    uint32_t recent_count;
} packet_police_sequence_t;

typedef struct {
    char callsign[SSDV_MAX_CALLSIGN + 1]; // empty = don't check
    packet_police_sequence_t sequences[PACKET_POLICE_CONTENT_TYPES];
    packet_police_stats_t stats;
} packet_police_t;

/* callsign = expected ssdv callsign, or NULL to skip that check */
void packet_police_init(packet_police_t *police, const char *callsign);

/*
pass in what lg_ccsds_profile_parse returned for one packet.
view is only read when parse_status == CCSDS_OK.
*/
packet_police_verdict_t packet_police_check(
    packet_police_t *police,
    ccsds_status_t parse_status,
    const lg_ccsds_profile_view_t *view
);

const char *packet_police_verdict_name(packet_police_verdict_t verdict);

#endif
