#!/usr/bin/env python3
"""Send flight-shaped ADCS telemetry to a GS serial port or print a sample.

    python3 src/test.py --dry-run --count 1
    python3 src/test.py --port /tmp/gs-tx --count 0

The 228-byte frame is CCSDS + LG profile + WESW record + ADCS v3 + CRC-32.
It fits the E22 240-byte air packet. No third-party packages are needed.
"""

import argparse
import contextlib
import os
import select
import struct
import sys
import termios
import time
import tty
import zlib


def telemetry_packet(sequence: int) -> bytes:
    adcs = bytearray(184)
    adcs[:4] = b"ADCS"
    adcs[4] = 3
    adcs[5] = 1  # SAFE
    struct.pack_into(">I", adcs, 8, sequence)
    struct.pack_into(">Q", adcs, 12, int(time.monotonic() * 1_000_000))
    struct.pack_into(">I", adcs, 20, 3)  # gyro + accelerometer valid
    struct.pack_into(">4f", adcs, 28, 0, 0, 0, 1)
    struct.pack_into(">4f", adcs, 44, 0, 0, 0, 1)
    struct.pack_into(">3f", adcs, 60, 0.01 * sequence, 0.02, 0.03)
    struct.pack_into(">f", adcs, 112, 0.95)
    struct.pack_into(">f", adcs, 180, 92)

    record = bytearray(202)
    record[:4] = b"WESW"
    record[4:8] = bytes((1, 2, 20, 0))
    struct.pack_into(">QH", record, 8, int(time.time() * 1_000_000), len(adcs))
    record[18:] = adcs

    profile = struct.pack(">BB6xII", 1, 0, 1, len(record))
    data_length = len(profile) + len(record) + 4
    header = struct.pack(">HHH", 0x0801, 0xC000 | (sequence & 0x3FFF), data_length - 1)
    packet = header + profile + record
    return packet + struct.pack(">I", zlib.crc32(packet) & 0xFFFFFFFF)


@contextlib.contextmanager
def serial_port(path: str, baud: int):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original = None
    try:
        original = termios.tcgetattr(fd)
        tty.setraw(fd, termios.TCSANOW)
        settings = termios.tcgetattr(fd)
        settings[2] &= ~(termios.CSTOPB | getattr(termios, "CRTSCTS", 0))
        settings[2] |= termios.CLOCAL | termios.CREAD
        settings[4] = settings[5] = getattr(termios, f"B{baud}")
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        yield fd
    finally:
        if original is not None:
            termios.tcsetattr(fd, termios.TCSANOW, original)
        os.close(fd)


def write_packet(fd: int, packet: bytes):
    remaining = memoryview(packet)
    deadline = time.monotonic() + 5
    while remaining:
        timeout = deadline - time.monotonic()
        if timeout <= 0 or not select.select([], [fd], [], timeout)[1]:
            raise TimeoutError("serial write timed out")
        try:
            written = os.write(fd, remaining)
        except BlockingIOError:
            continue
        if written == 0:
            raise OSError("serial port stopped accepting bytes")
        remaining = remaining[written:]
    termios.tcdrain(fd)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("E22_UART_DEVICE"))
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--count", type=int, default=10, help="0 means continuous")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--sequence", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.count < 0 or args.interval < 0 or not 0 <= args.sequence <= 0x3FFF:
        parser.error("invalid count, interval, or sequence")
    if args.baud <= 0 or not hasattr(termios, f"B{args.baud}"):
        parser.error("unsupported baud rate")
    if not args.dry_run and not args.port:
        parser.error("provide --port or set E22_UART_DEVICE")

    connection = contextlib.nullcontext(None) if args.dry_run else serial_port(args.port, args.baud)
    try:
        with connection as fd:
            sent = 0
            while args.count == 0 or sent < args.count:
                sequence = (args.sequence + sent) & 0x3FFF
                packet = telemetry_packet(sequence)
                if fd is not None:
                    write_packet(fd, packet)
                print(f"{'Preview' if args.dry_run else 'Sent'} ADCS sequence={sequence}", flush=True)
                sent += 1
                if args.count == 0 or sent < args.count:
                    time.sleep(args.interval)
    except KeyboardInterrupt:
        return 130
    except (OSError, termios.error) as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
