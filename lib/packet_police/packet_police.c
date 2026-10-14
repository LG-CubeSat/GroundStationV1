#include "packet_police.h"

#include <string.h>

#define SEQUENCE_MASK CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT // 14 bit counter, wraps 16383 -> 0
#define SEQUENCE_HALF ((SEQUENCE_MASK + 1U) / 2U)

void packet_police_init(packet_police_t *police, const char *callsign)
{
    memset(police, 0, sizeof(*police));
    if (callsign != NULL) {
        strncpy(police->callsign, callsign, sizeof(police->callsign) - 1);
    }
}

static bool recently_seen(const packet_police_sequence_t *seq, uint16_t count)
{
    uint32_t n = seq->recent_count < PACKET_POLICE_RECENT ? seq->recent_count : PACKET_POLICE_RECENT;
    for (uint32_t i = 0; i < n; i++) {
        if (seq->recent[i] == count) return true;
    }
    return false;
}

static void remember(packet_police_sequence_t *seq, uint16_t count)
{
    seq->recent[seq->recent_count % PACKET_POLICE_RECENT] = count;
    seq->recent_count++;
    seq->last = count;
    seq->seen_any = true;
}

/* false = duplicate. each content type (apid) has its own counter */
static bool track_sequence(packet_police_t *police, packet_police_sequence_t *seq, uint16_t count)
{
    if (!seq->seen_any) {
        remember(seq, count);
        return true;
    }
    if (recently_seen(seq, count)) return false;

    uint16_t ahead = (uint16_t)((count - seq->last) & SEQUENCE_MASK);
    if (ahead < SEQUENCE_HALF) {
        police->stats.missing += ahead - 1U; // e.g. 41 -> 44 means 42 and 43 never came
    } else {
        police->stats.resyncs++; // jumped backwards, satellite likely rebooted and restarted at 0
    }
    remember(seq, count);
    return true;
}

static packet_police_verdict_t check_ssdv(const packet_police_t *police, const lg_ccsds_profile_view_t *view)
{
    if (view->payload_length != LG_CCSDS_SSDV_PACKET_SIZE) return PACKET_POLICE_REJECT_BAD_SSDV;

    // ssdv_dec_is_packet writes fixes back into the packet, so check a copy
    uint8_t packet[SSDV_PKT_SIZE];
    memcpy(packet, view->payload, view->payload_length);

    int errors = 0;
    if (ssdv_dec_is_packet(packet, (int)view->payload_length, &errors) != 0) {
        return PACKET_POLICE_REJECT_BAD_SSDV;
    }

    if (police->callsign[0] != '\0') {
        ssdv_packet_info_t info;
        ssdv_dec_header(&info, packet);
        if (strcmp(info.callsign_s, police->callsign) != 0) return PACKET_POLICE_REJECT_CALLSIGN;
    }
    return PACKET_POLICE_ACCEPT;
}

packet_police_verdict_t packet_police_check(
    packet_police_t *police,
    ccsds_status_t parse_status,
    const lg_ccsds_profile_view_t *view)
{
    packet_police_verdict_t verdict;
    police->stats.received++;

    if (parse_status == CCSDS_ERROR_CRC) {
        verdict = PACKET_POLICE_REJECT_CRC;
    } else if (parse_status != CCSDS_OK || view == NULL ||
               (unsigned)view->metadata.content_type >= PACKET_POLICE_CONTENT_TYPES) {
        verdict = PACKET_POLICE_REJECT_MALFORMED;
    } else {
        packet_police_sequence_t *seq = &police->sequences[view->metadata.content_type];
        if (!track_sequence(police, seq, view->metadata.sequence_count)) {
            verdict = PACKET_POLICE_REJECT_DUPLICATE;
        } else if (view->metadata.content_type == LG_CCSDS_CONTENT_SSDV) {
            verdict = check_ssdv(police, view);
        } else {
            verdict = PACKET_POLICE_ACCEPT;
        }
    }

    if (verdict == PACKET_POLICE_ACCEPT) {
        police->stats.accepted++;
    } else {
        police->stats.rejected[verdict]++;
    }
    return verdict;
}

const char *packet_police_verdict_name(packet_police_verdict_t verdict)
{
    switch (verdict) {
        case PACKET_POLICE_ACCEPT:
            return "accepted";
        case PACKET_POLICE_REJECT_MALFORMED:
            return "malformed";
        case PACKET_POLICE_REJECT_CRC:
            return "crc mismatch";
        case PACKET_POLICE_REJECT_DUPLICATE:
            return "duplicate";
        case PACKET_POLICE_REJECT_BAD_SSDV:
            return "bad ssdv";
        case PACKET_POLICE_REJECT_CALLSIGN:
            return "wrong callsign";
        default:
            return "unknown";
    }
}
