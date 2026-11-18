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

The **Run end-to-end link check** button sends ADCS Safe and waits for the
matching ADCS ACK plus fresh telemetry showing SAFE mode and valid gyro and
accelerometer bits. Other buttons show their returned ACK/NACK or route status.
An ADCS ACK means the command was accepted by its queue; later telemetry is the
evidence of the state change. The photo result currently confirms only that the
OBC queued the mission request, not that the image was captured or downlinked.
Command sequences use the web request ID; the current flight command envelope is an eight-byte native
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

## Check the E22 link after a wiring short

Disconnect power and inspect the radio and PCB before retrying. With power off,
check that VCC and GND are not shorted. If a component heats up or the supply
voltage collapses when power returns, switch it off and resolve that first.

Stop the running `GSMain` process so it releases the Mac USB serial port. On the
Mac, find the USB E22 port and start the peer before the Pi test:

```sh
ls /dev/cu.usbserial*
python3 -m pip show pyserial || python3 -m pip install pyserial
python3 tests/radio_hardware_peer.py --port /dev/cu.usbserial-1130 --timeout 120
```

Replace the port with the one from `ls`. In the FlightSoftwareV1.1 checkout on
the Pi, stop any manually started OBC supervisor as well as the service, then:

```sh
sudo systemctl stop obc.service
pgrep -af 'obc_supervisor|obc_radio|radio_hardware_test'
sudo systemctl start pigpiod
pigs m 25 r
pigs pud 25 o
cmake -S . -B build-hw -DHW_MODE=ON -DBUILD_TESTS=ON
cmake --build build-hw --target radio_hardware_test -j4
E22_SWAPPED_UART=1 ./build-hw/bin/radio_hardware_test
```

A **PASS** means the Pi's production driver sent a CCSDS frame through its
onboard E22, the Mac USB E22 received and echoed it, and the Pi received the
echo. `LOCAL FAIL` is before the Mac peer matters. `Local TX completed` followed
by `END-TO-END FAIL` means the outbound UART write completed, but the RF round
trip was not confirmed. If the Mac shows `Received valid ...` but the Pi still
fails, concentrate on the return path. This short-range check does not establish
range or long-term reliability.

## Test the connected Pi and real ADCS board

Leave the flashed STM32 ADCS powered and connected to the Pi I2C bus. Do not
start `adcs_sim` for this test. On the Pi, enable `/dev/i2c-1` and confirm the
ADCS slave appears at `0x2A`. The hardware OBC build requires `pigpiod_if2`
headers and library for the E22 driver. Then, from the flight repo on the Pi:

```sh
git submodule update --init --recursive
cmake -S . -B build-hw -DHW_MODE=ON -DBUILD_TESTS=OFF
cmake --build build-hw -j4
sudo systemctl stop obc.service
sudo systemctl start pigpiod
pigs m 25 r
pigs pud 25 o
session_dir=$(mktemp -d)
E22_SWAPPED_UART=1 MISSION_STATE_PATH="$session_dir/mission.state" \
  OBC_TELEMETRY_LOG_PATH="$session_dir/telemetry.bin" \
  MISSION_ASCENT_WAIT_SEC=100000 MISSION_TELEMETRY_DOWNLINK_INTERVAL_SEC=3 \
  MISSION_AUTONOMY_ENABLED=0 \
  ./build-hw/bin/obc_supervisor
```

The Pi driver uses `/dev/serial0` by default and configures its E22 for 9600
8N1 UART, 2.4 kbps air rate, transparent mode, and channel 23 (433.125 MHz).
This PCB uses swapped UART TX/RX, so keep `E22_SWAPPED_UART=1` on the Pi.
The known-good E22 AUX setup is GPIO25 as input with the internal pull disabled, shown above.
Start `pigpiod` before setting the pin. Set the ground E22 to matching settings before starting
the ground station. On the Mac, run `GSMain` with `E22_UART_DEVICE` as shown
above, then run `GSEyecandy` in another terminal. Do not set `GS_SIM_RF`.

The default mission downlink now requests only the newest ADCS snapshot every
3 seconds. The UI shows RF arrival age and the Pi receive timestamp age
separately, and keeps the newest timestamp if delayed packets arrive. Sync the
Pi and Mac clocks before using sample age or the end-to-end link check; compare
`date -u` on both machines. Set `MISSION_TELEMETRY_MODE=history` only when you
intend to replay the stored log.

Open the UI, confirm the ADCS sequence advances and OBC sample age stays low,
then click **Run end-to-end link check**. A PASS exercises the web UI, ground
serial/E22, flight E22/OBC, physical I2C/CSP, ADCS command ACK, and telemetry
back to the browser. The STM32 BMI270 driver has not been exercised on the
board; if it cannot initialize the sensor, command responses and telemetry may
appear while the gyro/accelerometer-valid check fails.
`MISSION_AUTONOMY_ENABLED=0` prevents the OBC's automatic Point to Sun action
from overriding your manual Safe command during this bench test.

## Run the live full-system simulator through this UI

This uses the real GS executables, OBC supervisor, ADCS simulator, CSP, command
handler, mission telemetry, and CCSDS frames. `GSMain` talks directly to the
flight radio simulator over localhost UDP; no serial bridge or fixture script is
needed. Build flight software in `FlightSoftwareV1.1` and GS here, then run four
terminals:

```sh
# FlightSoftwareV1.1, terminal 1
cmake -S . -B build -DHW_MODE=OFF
cmake --build build --target obc_supervisor adcs_sim
session_dir=$(mktemp -d)
MISSION_STATE_PATH="$session_dir/mission.state" \
  OBC_TELEMETRY_LOG_PATH="$session_dir/telemetry.bin" \
  MISSION_ASCENT_WAIT_SEC=100000 MISSION_TELEMETRY_DOWNLINK_INTERVAL_SEC=3 \
  MISSION_AUTONOMY_ENABLED=0 \
  ./build/bin/obc_supervisor

# FlightSoftwareV1.1, terminal 2
./build/apps/adcs/adcs_sim

# GroundStationV1, terminal 3
GS_SIM_RF=1 ./build/GSMain

# GroundStationV1, terminal 4
./build/GSEyecandy
```

Open `http://127.0.0.1:8080`, wait for a changing ADCS sequence, then click
**Run end-to-end link check**. PASS means the matching ADCS ACK came back through
OBC and fresh telemetry says SAFE with gyro and accelerometer validity bits set.
The new `session_dir` avoids old mission state and telemetry on repeated runs.
The simulator radio defaults to UDP downlink 5600 and uplink
5601. For alternate ports, set `RADIO_SIM_TX_PORT`/`RADIO_SIM_RX_PORT` on flight
and matching `GS_SIM_DOWNLINK_PORT`/`GS_SIM_UPLINK_PORT` on `GSMain`.

`just test` remains a standalone serial fixture for GS development. It does
not run the flight software or exercise the ADCS command loop. Use the live
full-system simulator above for an end-to-end software check.

## Remaining system work

- Confirm real Pi↔ADCS I2C/CSP and E22↔E22 operation on hardware.
- Add raw BMI270 gyro and accelerometer values to ADCS telemetry if those are
  required on the ground. The current packet carries validity and estimated
  angular rate, but no raw acceleration vector.
- Test packet loss, duplicate commands, restart recovery, and photo downlink
  on the integrated system.
