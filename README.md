# ESP32NOW — ESP-NOW Range Indicator

Two ESP32 boards discover each other over ESP-NOW and blink their **D2 / GPIO 2
LEDs while messages are passing in both directions**. When contact stops, the
LEDs turn off after approximately **3 seconds**. They resume blinking
automatically when communication returns.

Upload the **same firmware to both boards**. There are no sender/receiver roles,
Wi-Fi credentials, or MAC addresses to enter.

> This project indicates working radio communication. It does not measure
> physical distance or guarantee a particular range. Walls, interference,
> antenna orientation, and the power supply affect communication.

## System diagram

![Two separately powered ESP32 boards exchange ESP-NOW heartbeats and drive their D2 LEDs.](docs/diagrams/system.svg)

[Editable diagram source](docs/diagrams/system.mmd)

The boards communicate directly over their Wi-Fi radios. No router, internet
connection, or signal wire between boards is needed. Each board has its own
power connection. CH340 handles USB-to-serial communication for uploading and
Serial Monitor; the ESP32 handles ESP-NOW.

## Hardware and configuration

This configuration targets a **classic ESP32 development board with CH340,
30 pins, USB Type-C, and 4 MB flash**, using PlatformIO's `esp32dev` profile.
It is not a configuration for ESP32-S3, C3, or other chip variants.

| Item | Configuration |
| --- | --- |
| PlatformIO environment / board | `esp32dev` |
| Framework | Arduino |
| LED output | D2 / GPIO 2 |
| LED polarity | Active HIGH by default |
| Wi-Fi interface | Station, without joining a router |
| Wi-Fi channel | 6 on both boards |
| Radio mode / rate | Espressif LR only / 250 kbit/s |
| Normal new-heartbeat interval | 400–600 ms, with randomized timing |
| ACK retry delay / limit | 300–400 ms / 2 retries after the first attempt |
| Contact timeout | 3,000 ms |
| LED toggle interval | 250 ms: two complete blinks per second |
| Upload / Serial Monitor baud | 115200 |
| USB serial port | Automatically detected |

**Required hardware:** two supported ESP32 boards and reliable USB power
connections. Use a USB **data** cable for uploading. Both boards must remain
powered during the communication test.

The firmware assumes your board has a controllable LED on GPIO 2. Board clones
can differ: check the board's LED connection if Serial Monitor reports contact
but the LED does not blink. A permanently lit power LED cannot be controlled
by this sketch. The two LEDs blink independently, so their timing may differ.

## How communication is confirmed

Each board sends a small broadcast heartbeat containing its boot identifier,
sequence number, and acknowledgement of the latest heartbeat received from the
other board. Both boards use the same channel and application group value.

![Heartbeat and acknowledgement exchange confirms communication in both directions.](docs/diagrams/handshake.svg)

[Editable diagram source](docs/diagrams/handshake.mmd)

A successful `esp_now_send()` call alone does **not** start blinking. The
other board must acknowledge a heartbeat that this board actually submitted
recently. This checks the path in both directions.

The source MAC identifies the discovered board. Boot identifiers separate
messages from different runs; sequence numbers identify fresh messages.
The firmware ignores invalid, self-originated, and reordered heartbeats.
A retried probe keeps its sequence number but carries the latest acknowledgement.
It is not counted as a new heartbeat; its acknowledgement fields are processed
if they confirm a new, recent probe. Repeating an old acknowledgement cannot
keep the LEDs blinking. Stale or unknown acknowledgements cannot refresh contact.
Wi-Fi receive callbacks copy messages into a queue; the main loop handles
contact tracking and LED output without blocking the radio task.

![LED waits off, blinks during confirmed contact, and turns off after contact loss.](docs/diagrams/led-states.svg)

[Editable diagram source](docs/diagrams/led-states.mmd)

## Long-range mode and crowded environments

**Both boards now use Espressif LR-only mode at 250 kbit/s on channel 6.**
The firmware calls `esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR)` and
selects `WIFI_PHY_RATE_LORA_250K` with `esp_wifi_config_espnow_rate()` after
radio and ESP-NOW initialization. Despite the enum name, this is Espressif's
Wi-Fi LR mode, not a separate LoRa radio.

LR improves receive sensitivity at a lower data rate. It is suitable for small
heartbeats, but it still shares the 2.4 GHz band and cannot remove interference.
There is no guaranteed distance or guaranteed improvement in a particular
crowd. See [Espressif's LR documentation](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html#long-range-lr).

At startup, the firmware reads back the active protocol and channel, checks
radio configuration errors, and prints:

```text
Radio: LR only | PHY rate: 250 kbit/s | Max ACK retries: 2
```

### Channel selection

Channel 6 is the default. If it is busy at your location, change `WIFI_CHANNEL`
to `11`, rebuild, and upload to **both** boards. Compare communication at the
same position using the RX, ACK, and RETRY counters. Neither channel is always
quieter. The firmware uses a fixed channel and does not scan or hop channels.

**Upload this updated firmware to both boards before testing.** Older firmware
using channel 1 and standard Wi-Fi mode cannot communicate with this LR-only,
channel-6 configuration. Keep `USE_LONG_RANGE` identical on both boards; set
it to `false` on both to use standard BGN mode at 1 Mbit/s for comparison.

### Acknowledgement retries

Only one probe is outstanding at a time. If its ACK has not arrived after
300–400 ms, the same probe is sent again, up to **two additional attempts**.
The wait includes randomized jitter to reduce repeated collisions. A fresh
ACK cancels the remaining retries. When the retry budget is exhausted, the
board continues discovering its peer with a new probe rather than flooding
retries indefinitely.

A retry retains the probe sequence and updates the piggyback acknowledgement.
This allows two boards that transmitted simultaneously to acknowledge each
other through their retries. The normal new-heartbeat period is 400–600 ms;
an unacknowledged cycle can take longer while its retries finish. The LED still
switches off after three seconds without a fresh two-way confirmation.

For the physical setup, keep the PCB antennas clear of metal and your body,
use reliable power, and test antenna placement and height at the intended site.
Lower data rates occupy the channel for longer, so retries are deliberately
bounded. This firmware does not increase transmit power beyond the driver's
default or modify the board's antenna hardware.

## Open and build in VS Code

1. Install VS Code and the **PlatformIO IDE** extension.
2. Clone this repository, or download and extract it.
3. Open the **project root folder**, containing `platformio.ini`, in VS Code.
4. Select the **esp32dev** environment in PlatformIO.
5. Click **Build**. PlatformIO installs the required platform and framework.

The existing local project folder may be named `ESP32SNOW`; the GitHub
repository is named `ESP32NOW`. Either folder name works. Keep the whole
project, including `include/range_link.h`, rather than copying only `main.cpp`.

### PlatformIO configuration

The functional settings in [platformio.ini](platformio.ini) are:

```ini
[platformio]
default_envs = esp32dev

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 115200
monitor_dtr = 0
monitor_rts = 0
```

Serial Monitor starts with DTR and RTS deasserted to avoid holding the board
in reset. No fixed serial port is stored in the project.

## Flash the boards one at a time

1. Connect **board A** to the PC with a USB data cable.
2. Click PlatformIO **Upload** for environment **esp32dev**.
3. Disconnect board A and connect **board B**.
4. Upload the **same project** to board B.
5. Power both boards and place them near each other.
6. Open **Serial Monitor at 115200 baud** on whichever board is connected
   to the PC.

If both are connected to the PC at once, select the intended upload port in
PlatformIO. Each board needs only its own power supply for normal operation.

### Terminal commands

Run these from the project root using a terminal where `pio` is available
(for example, PlatformIO's terminal in VS Code):

```sh
pio run --environment esp32dev
pio device list
pio run --environment esp32dev --target upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Replace `/dev/ttyUSB0` with the detected port: it may be another `/dev/ttyUSB*`
on Linux, `COM*` on Windows, or `/dev/cu.*` on macOS. Close Serial Monitor
before uploading if it keeps the port busy.

## Expected behavior and physical test

| Test | Expected result |
| --- | --- |
| Only one board powered | Its D2 LED stays off |
| Both boards powered nearby | Both D2 LEDs blink after heartbeat exchange |
| One board switched off | Remaining board's LED turns off in about 3 seconds |
| Boards separated until communication fails | LEDs stop blinking after timeout |
| Other board powered again or brought back | Blinking resumes automatically |
| A board rebooted | Fresh contact is established again |

Typical Serial Monitor output is:

```text
Waiting for the other board...
IN RANGE: D2 LED blinking
IN RANGE | TX attempts=10 RETRY=2 RX=9 ACK=8
CONTACT LOST: LED off
WAITING / OUT OF RANGE | TX attempts=18 RETRY=6 RX=9 ACK=8
```

The numbers above are illustrative. A status line is printed every two seconds.

| Counter / message | Meaning |
| --- | --- |
| `TX attempts` | Heartbeat send attempts; this does not prove reception |
| `RETRY` | Additional transmission attempts for missing ACKs |
| `RX` | Fresh heartbeats from the discovered peer; retries are not counted twice |
| `ACK` | Fresh acknowledgements of recently submitted heartbeats |
| `IN RANGE` | Recent two-way contact; LED blinking enabled |
| `WAITING / OUT OF RANGE` | Fresh two-way contact is absent; LED off |

## Change settings

| Setting | File | Default | Purpose |
| --- | --- | --- | --- |
| `LED_PIN` | [src/main.cpp](src/main.cpp) | `2` | Controllable LED GPIO |
| `LED_ACTIVE_LOW` | [src/main.cpp](src/main.cpp) | `false` | Set `true` if LOW turns the LED on |
| `WIFI_CHANNEL` | [src/main.cpp](src/main.cpp) | `6` | Try 11 if needed; keep the same on both boards |
| `USE_LONG_RANGE` | [src/main.cpp](src/main.cpp) | `true` | LR-only at 250 kbit/s; keep identical on both boards |
| `kGroup` | [include/range_link.h](include/range_link.h) | `0x32B11A01` | Keep the same for this pair |
| `kTimeoutMs` | [include/range_link.h](include/range_link.h) | `3000` | Contact-loss timeout |
| `kBlinkMs` | [include/range_link.h](include/range_link.h) | `250` | Time between LED toggles |
| `kRetryBaseMs` | [include/range_link.h](include/range_link.h) | `300` | Minimum ACK wait between attempts |
| `kRetryJitterMs` | [include/range_link.h](include/range_link.h) | `100` | Up to 100 ms additional randomized retry delay |
| `kMaxRetries` | [include/range_link.h](include/range_link.h) | `2` | Extra attempts after the first transmission |

Rebuild and upload after changing settings. Use a different `kGroup` for another
pair operating nearby. This is a simple **unencrypted** broadcast demo, and the
group value is a packet filter, not authentication. It assumes two participating
boards per group. Do not connect either board to a router from this sketch;
a router connection can change the radio channel.

## Tests and verification

The same `RangeLink` class is used in the firmware and host tests. The host
suite simulates two boards and exercises the contact and LED logic, including:

- A single powered board and two-board discovery.
- Two-way contact, blink timing, the timeout boundary, and reconnection.
- One-direction failure and moderate packet loss.
- Peer/local reboots and equal boot identifiers on distinct boards.
- Invalid, self-originated, duplicate, and reordered packets.
- Stale acknowledgements and acknowledgements of unsent packets.
- Clock/sequence rollover and an unrelated third board.
- Bounded retry scheduling, ACK cancellation, and recovery of a dropped probe.
- Fresh ACKs carried by duplicate retries, jitter across clock rollover, and
  repeated retries that cannot preserve stale contact.

On Linux with Python 3 and g++ installed, run:

```sh
python3 test/run_host_tests.py
python3 test/verify_readme.py
pio run --environment esp32dev
```

The tests compile with strict warnings, AddressSanitizer, and
UndefinedBehaviorSanitizer. **All 22 software scenarios and the `esp32dev`
firmware build passed during preparation of this project.** Physical radio
reception, power stability, and onboard LED operation still require the
board-to-board test after flashing.

The README diagrams are checked SVG images generated from the linked Mermaid
sources. They do not need GitHub to parse Mermaid when the page is opened.
`test/verify_readme.py` checks image files, local links, Markdown fences, and
the documented configuration and defaults against the firmware. See
[diagram maintenance](docs/diagrams/README.md) for regeneration instructions.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `RX=0`, LED off | Power both boards, upload matching firmware, keep channel/group/LR settings identical, and place boards nearby |
| `RX` increases but `ACK` stays at zero | Verify both boards remain running and messages can pass both ways; compare their serial logs |
| `IN RANGE` shown but LED does not blink | Check `LED_PIN`, `LED_ACTIVE_LOW`, and whether the board has a controllable GPIO 2 LED |
| Repeated `Brownout detector was triggered` | Supply voltage is dropping; try a reliable cable and USB source, remove external loads, and check the board power circuit |
| Upload port missing | Use a data-capable cable and check CH340 USB-serial support on the PC |
| Permission denied on Linux | Grant access to the detected serial device; see below |
| Upload cannot connect | Close Serial Monitor; hold BOOT during connection, then release it when writing starts |

For temporary Linux serial-port access, use your actual detected port:

```sh
sudo setfacl -m u:$(id -un):rw /dev/ttyUSB0
```

This permission may reset when the board is unplugged. The firmware keeps
brownout protection enabled; repeated resets must be resolved before radio
communication can be tested.

## Project layout

```text
ESP32NOW/
├── platformio.ini                 # Board, framework, upload and monitor settings
├── src/main.cpp                   # ESP-NOW callbacks, startup, queue and GPIO output
├── include/range_link.h           # Heartbeat, contact tracking and LED timing
├── test/host/test_range_link.cpp   # 22 deterministic software scenarios
├── test/run_host_tests.py          # Host compiler/test runner
├── test/verify_readme.py           # README links, diagram and setting checks
├── docs/diagrams/                  # Editable Mermaid sources and checked SVGs
├── .gitignore                     # Excludes build output and local editor data
└── README.md                      # Setup, diagrams and troubleshooting
```

## References

- [PlatformIO: Espressif ESP32 Dev Module](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html)
- [Espressif: long-range radio mode](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html#long-range-lr)
- [Espressif: ESP-NOW API and acknowledgement guidance](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/network/esp_now.html)
- [Espressif: brownout and other fatal errors](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/fatal-errors.html)
