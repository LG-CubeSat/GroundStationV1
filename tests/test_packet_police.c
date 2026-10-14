#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "packet_police.h"
#include "test_jpeg_data.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static lg_ccsds_profile_view_t make_view(lg_ccsds_content_type_t type, uint16_t sequence, const uint8_t *payload, size_t length)
{
    lg_ccsds_profile_view_t view;
    memset(&view, 0, sizeof(view));
    view.metadata.content_type = type;
    view.metadata.sequence_count = sequence;
    view.payload = payload;
    view.payload_length = length;
    return view;
}

/* first real ssdv packet of the test jpeg, same size the ccsds profile expects */
static int make_ssdv_packet(uint8_t *out, const char *callsign)
{
    ssdv_t ssdv;
    uint8_t pkt[SSDV_PKT_SIZE];
    size_t offset = 0;
    char c;

    if (ssdv_enc_init(&ssdv, SSDV_TYPE_NORMAL, (char *)callsign, 1, 4, LG_CCSDS_SSDV_PACKET_SIZE) != SSDV_OK) return -1;
    ssdv_enc_set_buffer(&ssdv, pkt);

    while ((c = ssdv_enc_get_packet(&ssdv)) == SSDV_FEED_ME) {
        size_t chunk = test_jpeg_data_len - offset;
        if (chunk == 0) return -1;
        if (chunk > 128) chunk = 128;
        ssdv_enc_feed(&ssdv, (uint8_t *)(test_jpeg_data + offset), chunk);
        offset += chunk;
    }
    if (c != SSDV_OK) return -1;

    memcpy(out, pkt, LG_CCSDS_SSDV_PACKET_SIZE);
    return 0;
}

int main(void)
{
    packet_police_t police;
    uint8_t telemetry[16] = {0};
    lg_ccsds_profile_view_t view;

    packet_police_init(&police, "COM");

    /* sequence tracking */
    view = make_view(LG_CCSDS_CONTENT_RAW_TELEMETRY, 0, telemetry, sizeof(telemetry));
    check("first packet accepted", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("same sequence again is a duplicate", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_REJECT_DUPLICATE);

    view = make_view(LG_CCSDS_CONTENT_RAW_TELEMETRY, 1, telemetry, sizeof(telemetry));
    check("next sequence accepted", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("no gap, nothing missing", police.stats.missing == 0);

    view = make_view(LG_CCSDS_CONTENT_RAW_TELEMETRY, 4, telemetry, sizeof(telemetry));
    check("packet after a gap still accepted", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("1 -> 4 counts 2 missing", police.stats.missing == 2);

    view = make_view(LG_CCSDS_CONTENT_RAW_TELEMETRY, 2, telemetry, sizeof(telemetry));
    check("backwards jump treated as reboot, accepted", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("reboot counted as resync", police.stats.resyncs == 1);

    /* ccsds parse failures */
    check("crc failure rejected", packet_police_check(&police, CCSDS_ERROR_CRC, NULL) == PACKET_POLICE_REJECT_CRC);
    check("format failure rejected", packet_police_check(&police, CCSDS_ERROR_INVALID_FORMAT, NULL) == PACKET_POLICE_REJECT_MALFORMED);

    /* sequence wraps 16383 -> 0 */
    packet_police_t wrap;
    packet_police_init(&wrap, NULL);
    view = make_view(LG_CCSDS_CONTENT_COMMAND, 16383, telemetry, sizeof(telemetry));
    packet_police_check(&wrap, CCSDS_OK, &view);
    view = make_view(LG_CCSDS_CONTENT_COMMAND, 0, telemetry, sizeof(telemetry));
    check("16383 -> 0 wrap accepted", packet_police_check(&wrap, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("wrap is not a gap or reboot", wrap.stats.missing == 0 && wrap.stats.resyncs == 0);

    /* ssdv */
    uint8_t ssdv[LG_CCSDS_SSDV_PACKET_SIZE];
    check("test ssdv packet built", make_ssdv_packet(ssdv, "COM") == 0);

    view = make_view(LG_CCSDS_CONTENT_SSDV, 0, ssdv, sizeof(ssdv));
    check("real ssdv packet accepted", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_ACCEPT);
    check("ssdv has its own sequence counter", police.stats.missing == 2);

    view = make_view(LG_CCSDS_CONTENT_SSDV, 1, ssdv, sizeof(ssdv) - 1);
    check("wrong size ssdv rejected", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_REJECT_BAD_SSDV);

    uint8_t garbage[LG_CCSDS_SSDV_PACKET_SIZE];
    memset(garbage, 0xAB, sizeof(garbage));
    view = make_view(LG_CCSDS_CONTENT_SSDV, 2, garbage, sizeof(garbage));
    check("garbage ssdv rejected", packet_police_check(&police, CCSDS_OK, &view) == PACKET_POLICE_REJECT_BAD_SSDV);

    packet_police_t other;
    packet_police_init(&other, "XYZ");
    view = make_view(LG_CCSDS_CONTENT_SSDV, 0, ssdv, sizeof(ssdv));
    check("ssdv from another callsign rejected", packet_police_check(&other, CCSDS_OK, &view) == PACKET_POLICE_REJECT_CALLSIGN);

    /* stats add up */
    uint32_t rejected = 0;
    for (int i = 0; i < PACKET_POLICE_VERDICT_COUNT; i++) rejected += police.stats.rejected[i];
    check("received = accepted + rejected", police.stats.received == police.stats.accepted + rejected);

    if (failures != 0) {
        printf("packet_police_test: %d FAILED\n", failures);
        return 1;
    }
    printf("packet_police_test: PASS\n");
    return 0;
}
