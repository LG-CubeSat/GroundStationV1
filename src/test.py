#!/usr/bin/env python3
"""Send test telemetry to GroundStation over a serial link (macOS/Linux).

Examples:
    python3 src/test.py --port /dev/cu.usbserial-XXXX
    python3 src/test.py --port /dev/ttyUSB0 --count 0 --interval 1
    python3 src/test.py --dry-run --count 1

Use the transmitting adapter connected to the ground station's receiver, or
the other end of a paired virtual serial link. The E22 must already be in
transparent mode with matching radio settings and M0/M1 held low. This script
does not configure the radio's GPIO or registers. No third-party packages needed.

Wire format: 6-byte CCSDS header + 16-byte LG profile header + 20-byte packed
telemetry + 4-byte CRC-32. Headers/CRC are big-endian; telemetry is little-endian.
See lib/ccsds/src/profile.c, lib/serial.h, and src/main.c.
"""

import argparse
import contextlib
import math
import os
import select
import struct
import sys
import termios
import time
import tty
import zlib


def integer_range(minimum, maximum):
    def parse(value):
        number = int(value)
        if not minimum <= number <= maximum:
            raise argparse.ArgumentTypeError(f"must be {minimum} through {maximum}")
        return number

    return parse


def finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise argparse.ArgumentTypeError("must be finite")
    return number


def float32(value):
    number = finite_float(value)
    try:
        struct.pack("<f", number)
    except (OverflowError, struct.error):
        raise argparse.ArgumentTypeError("must fit in a 32-bit float") from None
    return number


def telemetry_packet(test_run, sample, temperature_c, battery_v, altitude_m, sequence):
    """Match serialize_telemetry() and lg_ccsds_profile_build(raw, seq, 1, ...)."""
    payload = struct.pack("<IIffi", test_run, sample, temperature_c, battery_v, altitude_m)
    # Version 1, raw telemetry (content 0), no compression, one item.
    profile = struct.pack(">BB6xII", 1, 0, 1, len(payload))
    data_length = len(profile) + len(payload) + 4
    # Version 0, telemetry APID 0x001, secondary header, unsegmented sequence.
    header = struct.pack(">HHH", 0x0801, 0xC000 | (sequence & 0x3FFF), data_length - 1)
    packet = header + profile + payload
    return packet + struct.pack(">I", zlib.crc32(packet) & 0xFFFFFFFF)


@contextlib.contextmanager
def serial_port(path, baud):
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
        try:
            if original is not None:
                termios.tcsetattr(fd, termios.TCSANOW, original)
        finally:
            os.close(fd)


def write_packet(fd, packet):
    """Finish partial writes without adding delimiters or gaps within a frame."""
    remaining = memoryview(packet)
    deadline = time.monotonic() + 5.0
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


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--port", default=os.environ.get("E22_UART_DEVICE"),
                        help="transmitting serial device (default: E22_UART_DEVICE)")
    parser.add_argument("--baud", type=int, default=9600, help="UART baud rate (default: 9600)")
    parser.add_argument("--count", type=integer_range(0, sys.maxsize), default=10,
                        help="packets to send; 0 runs until Ctrl-C (default: 10)")
    parser.add_argument("--interval", type=finite_float, default=1.0,
                        help="seconds between completed writes (default: 1)")
    parser.add_argument("--test-run", type=integer_range(0, 0xFFFFFFFF),
                        default=int(time.time()) & 0xFFFFFFFF, help="run ID (default: Unix time)")
    parser.add_argument("--sample", type=integer_range(0, 0xFFFFFFFF), default=0,
                        help="first sample number; increments each packet (default: 0)")
    parser.add_argument("--sequence", type=integer_range(0, 0x3FFF), default=0,
                        help="first CCSDS sequence; wraps at 16384 (default: 0)")
    parser.add_argument("--temperature", type=float32, default=22.5, help="temperature in C")
    parser.add_argument("--battery", type=float32, default=4.1, help="battery voltage")
    parser.add_argument("--altitude", type=integer_range(-0x80000000, 0x7FFFFFFF),
                        default=100, help="altitude in meters")
    parser.add_argument("--dry-run", action="store_true", help="print packet hex without opening a port")
    args = parser.parse_args(argv)
    if args.interval < 0:
        parser.error("--interval must be nonnegative")
    if args.baud <= 0 or not hasattr(termios, f"B{args.baud}"):
        parser.error("--baud is not supported on this system")
    if not args.dry_run and not args.port:
        parser.error("provide --port or set E22_UART_DEVICE")

    connection = contextlib.nullcontext(None) if args.dry_run else serial_port(args.port, args.baud)
    try:
        with connection as fd:
            if fd is not None:
                print(f"Sending telemetry to {args.port} at {args.baud} baud, 8N1", flush=True)
            sent = 0
            while args.count == 0 or sent < args.count:
                sample = (args.sample + sent) & 0xFFFFFFFF
                sequence = (args.sequence + sent) & 0x3FFF
                packet = telemetry_packet(args.test_run, sample, args.temperature,
                                          args.battery, args.altitude, sequence)
                if fd is not None:
                    write_packet(fd, packet)
                print(f"{'Preview' if args.dry_run else 'Sent'} run={args.test_run} "
                      f"sample={sample} seq={sequence} bytes={len(packet)} "
                      f"hex={packet.hex()}", flush=True)
                sent += 1
                if args.count == 0 or sent < args.count:
                    time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
        return 130
    except (OSError, termios.error) as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
