#!/usr/bin/env python3
"""Mac USB E22 peer for the production radio_hardware_test executable.

Run this first on the Mac:
    python3 tests/radio_hardware_peer.py --port /dev/cu.usbserial-1130

Then run build-hw/bin/radio_hardware_test on the Pi. This script receives one
CCSDS frame, checks its CRC-32 and test marker, and echoes the exact frame.
Requires pyserial (`python3 -m pip install pyserial`).
Stop the ground station while this script owns the USB radio serial port.
The USB E22 must already have settings compatible with the Pi E22.
"""

import argparse
import sys
import time
import zlib


MARKER = b"E22_DRIVER_TEST "
HEADER = b"\x08\x01"  # raw telemetry APID from the flight radio queue
MIN_FRAME = 26
MAX_FRAME = 240


def take_test_frame(buffer: bytearray):
    while len(buffer) >= 6:
        if buffer[:2] != HEADER:
            del buffer[0]
            continue

        length = 6 + int.from_bytes(buffer[4:6], "big") + 1
        if length < MIN_FRAME or length > MAX_FRAME:
            del buffer[0]
            continue
        if len(buffer) < length:
            return None

        frame = bytes(buffer[:length])
        expected_crc = int.from_bytes(frame[-4:], "big")
        actual_crc = zlib.crc32(frame[:-4]) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            del buffer[0]
            continue

        del buffer[:length]
        if MARKER in frame:
            return frame
        print(f"Ignored other valid CCSDS frame ({length} bytes).", flush=True)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Mac USB serial device for the E22-400T22U")
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("Install pyserial: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    print(f"Listening for production radio frame on {args.port} (9600 8N1)...", flush=True)
    buffer = bytearray()
    deadline = time.monotonic() + args.timeout
    try:
        with serial.Serial(args.port, 9600, timeout=0.25) as usb:
            while time.monotonic() < deadline:
                data = usb.read(256)
                if data:
                    buffer.extend(data)
                frame = take_test_frame(buffer)
                if frame is None:
                    continue

                marker_at = frame.index(MARKER)
                print(f"Received valid {len(frame)}-byte CCSDS frame.", flush=True)
                print(frame[marker_at:-4].decode("ascii", errors="replace"), flush=True)
                time.sleep(0.3)  # let the Pi E22 finish its downlink transmission
                usb.write(frame)
                usb.flush()
                print("Echoed the same frame over the USB E22.", flush=True)
                return 0
    except serial.SerialException as exc:
        print(f"USB serial error: {exc}", file=sys.stderr)
        return 2

    print("TIMEOUT: no valid production test frame received.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
