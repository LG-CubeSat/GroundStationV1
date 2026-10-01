# Using the CCSDS library

This guide covers the public `ccsds` headers for a basic CubeSat ground station. The library handles packet framing, the LG project packet profile, stream reassembly, and optional CCSDS 121 compression. Your application still defines what the telemetry and command payload bytes mean.

## Include the API

For most applications, include the umbrella header:

```c
#include <ccsds/ccsds.h>
```

It includes the packet, profile, stream, compression, CRC, and status APIs. You can include a specific header such as `<ccsds/profile.h>` if you only need that part.

## Receive radio or UART data

A radio or UART read may contain part of a packet, one packet, or several packets. Use `ccsds_stream_parser_t` to assemble complete Space Packets from those byte chunks. Keep the parser and its storage for as long as the receiver runs; the storage must fit the largest packet you expect.

```c
uint8_t receive_storage[YOUR_MAX_PACKET_SIZE];
ccsds_stream_parser_t receiver;

ccsds_stream_parser_init(&receiver, receive_storage, sizeof receive_storage);
```

Each time the driver returns bytes, feed them to the parser with your packet callback:

```c
size_t packets_delivered;
ccsds_stream_parser_feed(&receiver, radio_bytes, byte_count,
                         on_packet, app_state, &packets_delivered);
```

The callback receives one complete packet at a time. For packets using the LG profile, call `lg_ccsds_profile_parse(packet, length, &view)` there. It checks the profile and CRC and fills a view with the packet header, sequence count, content type, item count, payload pointer, and payload length.

Handle the parsed content by `view.metadata.content_type`:

| Content type | Use |
| --- | --- |
| `LG_CCSDS_CONTENT_RAW_TELEMETRY` | Read mission-defined telemetry bytes from `view.payload`. |
| `LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY` | Decode `view.payload` with the compression settings in `view.metadata.compression`. |
| `LG_CCSDS_CONTENT_SSDV` | Pass the 256-byte payload to your SSDV image receiver. |
| `LG_CCSDS_CONTENT_COMMAND` | Interpret the payload according to your command format. |

The payload is not copied by parsing. Use it during the callback, or copy it before returning if you need to keep it. If a packet is invalid, log `ccsds_status_string(status)` and return `CCSDS_OK` from the callback when you want to drop that packet and continue receiving.

If the driver already provides one complete packet per read, you can call `lg_ccsds_profile_parse` directly and skip the stream parser. It expects exactly one complete packet.

## Send telemetry or commands

Use `lg_ccsds_profile_build` to wrap a payload in a complete, unsegmented packet. The function selects the packet type and APID from the content type. Supply a caller-owned output buffer large enough for the packet and transmit only the returned `output_length` bytes.

```c
lg_ccsds_profile_build(LG_CCSDS_CONTENT_RAW_TELEMETRY,
                       sequence_count, item_count, NULL,
                       telemetry, telemetry_length,
                       packet_buffer, sizeof packet_buffer, &packet_length);
```

For a command, use `LG_CCSDS_CONTENT_COMMAND` and place your mission-defined command bytes in the payload. Sequence counts are supplied by your application; they are 14-bit values, so wrap after `CCSDS_SPACE_PACKET_MAX_SEQUENCE_COUNT`.

The profile routes content types to these APIDs:

- Raw telemetry: `LG_CCSDS_APID_RAW_TELEMETRY` (`0x001`)
- Compressed telemetry: `LG_CCSDS_APID_COMPRESSED_TELEMETRY` (`0x002`)
- SSDV: `LG_CCSDS_APID_SSDV` (`0x003`)
- Commands: `LG_CCSDS_APID_COMMAND` (`0x100`)

Each profile packet has a 16-byte profile header and a 4-byte CRC in addition to the CCSDS primary header. A payload may be at most `LG_CCSDS_PROFILE_MAX_PAYLOAD_SIZE` bytes. SSDV profile packets require exactly 256 payload bytes and an `item_count` of 1.

## Optional compressed telemetry

Compression is a separate step from packet building. Configure CCSDS 121 with the mission's agreed sample format, then encode the samples before building a compressed telemetry packet. Set `item_count` to the number of samples and pass the same configuration to the profile builder. Supported settings are 1–32 bits per sample, block sizes of 8, 16, 32, or 64, and a reference sample interval of 1–4096. Restricted code options are only valid at 4 bits per sample or less.

Use `ccsds_121_encoded_bound` to size the compressed buffer, then call `ccsds_121_encode`. Pass its output and the same configuration to `lg_ccsds_profile_build` with `LG_CCSDS_CONTENT_COMPRESSED_TELEMETRY`.

On receive, the profile parser provides the sender's compression settings and sample count. Decode using those values:

```c
ccsds_121_decode(&view.metadata.compression,
                 view.payload, view.payload_length,
                 view.metadata.item_count,
                 decoded_samples, sizeof decoded_samples, &decoded_length);
```

Samples are stored most-significant byte first, using 1, 2, 3, or 4 bytes per sample depending on the configured bit width. Signed values use two's-complement representation. For widths that do not use every bit of the first byte, unused high bits must be zero. Use `ccsds_121_bytes_per_sample` to determine decoded storage needs.

## Status values

Most calls return `ccsds_status_t`. Check for `CCSDS_OK`; for failures, `ccsds_status_string(status)` gives a short message. A direct parse returns `CCSDS_ERROR_BUFFER_TOO_SMALL` for a truncated packet; the stream parser buffers partial reads and reports this error if its storage cannot hold a complete packet. Other receive failures include `CCSDS_ERROR_INVALID_FORMAT` for malformed packets and `CCSDS_ERROR_CRC` when profile integrity verification fails.

## Lower-level Space Packet API

If you are not using the LG profile, `ccsds_space_packet_build` and `ccsds_space_packet_parse` work with a CCSDS primary header and caller-defined packet data. `ccsds_space_packet_peek_size` reads the packet length from the six-byte primary header. For the LG profile, prefer the profile functions above so APID routing and CRC validation are handled consistently.
