# Ground station integration

`GSMain` reads CCSDS packets from the ground E22 serial port. It decodes the
flight OBC `WESW` telemetry record and ADCS v3 snapshot, then publishes the
decoded values to `GSEyecandy` over local NNG. The web UI runs at
`http://127.0.0.1:8080` and shows ADCS state, sensor validity, estimated rate,
attitude, faults, control output, and health counters.
Set `GS_HTTP_ADDRESS=127.0.0.1:18080` when port 8080 is occupied.

The five command buttons send flight-shaped RF command payloads:

| Button | Destination | Command ID |
|---|---|---:|
| Set ADCS Safe | CSP node 2, port 10 | 64 (mode 1) |
| Inhibit actuators | CSP node 2, port 10 | 68 (inhibit 1) |
| Reset IMU | CSP node 2, port 10 | 6 |
| Point to Sun | CSP node 2, port 10 | 7 |
| Take photo | OBC node 1 | 5 |

The UI reports **queued**, not delivered or executed. The flight OBC does not
yet downlink ADCS ACK/NACKs or a photo-command result. Command sequences use the
web request ID; the current flight command envelope is an eight-byte native
layout with a little-endian sequence. This should become an explicitly encoded
shared wire protocol before deployment on a different CPU ABI.

## Build and run on the ground laptop

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target GSMain GSEyecandy flight_protocol_test
ctest --test-dir build -R '^flight_protocol_test$' --output-on-failure
E22_UART_DEVICE=/dev/cu.usbserial-XXXX ./build/GSMain
./build/GSEyecandy
```

Use separate terminals for the two executables. The Mac E22 must already be
configured for the same 9600 8N1 UART, LoRa channel, air rate, and transparent
mode as the flight E22. `GSMain` does not configure its GPIO or E22 registers.

For a virtual serial demo, `just test` starts a paired `socat` PTY, both GS
programs, and `src/test.py`. The fixture now sends flight-shaped ADCS telemetry
instead of the older 20-byte temperature/battery/altitude demo payload.

## Remaining flight-side work

- Return ADCS ACK/NACK to the ground and show command status in the UI.
- Confirm real Pi↔ADCS I2C/CSP and E22↔E22 operation on hardware.
- Add raw BMI270 gyro and accelerometer values to ADCS telemetry if those are
  required on the ground. The current packet carries validity and estimated
  angular rate, but no raw acceleration vector.
- Test packet loss, duplicate commands, restart recovery, and photo downlink
  on the integrated system.
