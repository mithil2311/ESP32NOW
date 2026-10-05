# ESP32NOW: ESP-NOW LED range indicator

Open this complete project folder (`ESP32SNOW`) in PlatformIO in VS Code and
upload the same firmware to both ESP32 boards, one at a time. There are
no MAC addresses or sender/receiver roles to configure. `src/main.cpp` uses
`include/range_link.h`; keep both files in the project.

The current configuration targets the **ESP32 Development Board CH340, 30-pin,
USB Type-C**, using PlatformIO's generic `esp32dev` profile for a classic ESP32
with 4 MB flash. The CH340 provides the USB-to-serial connection; ESP-NOW runs
on the ESP32 itself. Upload and monitor speeds are 115200 baud. Serial Monitor
starts with DTR and RTS deasserted to avoid holding the board in reset.
PlatformIO detects the serial port automatically; no old board port is fixed
in the configuration. Select environment **esp32dev** when uploading.

Both boards broadcast a heartbeat about every 500 ms and echo the last message
received. The D2 / GPIO 2 LED blinks twice per second while fresh acknowledgements
confirm communication in both directions. It turns off about 3 seconds after
contact stops, including when the other board loses power. It starts blinking
again automatically when contact returns. The two LEDs blink independently.
The LED stays off when only one board is powered. "In range" means messages
are passing both ways; ESP-NOW does not supply a connection event or fixed
distance threshold.

## Upload and try it

1. Open this project folder in VS Code with PlatformIO installed.
2. Connect the first board by USB and click PlatformIO **Upload**.
3. Connect the second board and upload the same project again.
4. Power both boards. Open **Serial Monitor** at **115200 baud** for status.
5. Move one board away or switch it off to test the three-second timeout.

Keep the first board powered while testing the second. Normal monitor output:

```text
Waiting for the other board...
IN RANGE: D2 LED blinking
IN RANGE | TX attempts=... RX=... ACK=...
```

After switching the other board off, expect `CONTACT LOST: LED off` within
approximately three seconds. After switching it back on, the LEDs resume
blinking once fresh acknowledgements pass in both directions.

If both boards are connected by USB, select the intended port with PlatformIO's
device selector or set `upload_port` in `platformio.ini`.

From this project directory, the terminal build command is:

```sh
pio run
```

## Settings in src/main.cpp

- `LED_PIN = 2`: D2 / GPIO 2 controllable onboard LED. Change it if your
  board uses another pin. The permanently lit power LED is not controllable.
- `LED_ACTIVE_LOW = false`: set `true` if the onboard LED is inverted.
- `WIFI_CHANNEL = 1`: must match on both boards.

Settings in `include/range_link.h`:

- `kGroup`: must match on both boards; choose another value for another pair.
- `kTimeoutMs = 3000`: how long to wait before turning the LED off.
- `kBlinkMs = 250`: time between LED toggles.

The code supports Arduino-ESP32 2.x and 3.x receive callbacks. It needs no
additional libraries, router, internet connection or wiring between boards.
Communication range varies with obstacles, interference and antenna orientation;
this checks working communication rather than a fixed distance. This broadcast
demo is unencrypted and assumes only these two boards share the group ID.
The source MAC identifies the discovered board, so equal random boot IDs on
the two boards do not prevent discovery. Acknowledgements must refer to an
actually submitted, recent heartbeat. Duplicate, reordered and stale packets
cannot keep the LED blinking. Counters and timers tolerate 32-bit rollover.

Reference: [Espressif ESP-NOW documentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/network/esp_now.html).

## Diagnose a board that does not blink

The serial monitor prints startup stages and a status line every two seconds:

- `Starting Wi-Fi radio...`: radio initialization is about to begin.
- `Waiting for the other board...`: ESP-NOW initialization succeeded.
- `TX attempts`: heartbeat send attempts; this alone does not prove reception.
- `RX`: valid heartbeats received from the other board.
- `ACK`: fresh acknowledgements proving messages passed in both directions.
- `IN RANGE`: acknowledgements are recent and GPIO 2 is blinking.

If `Brownout detector was triggered` repeats, the board is resetting because
its supply voltage dropped. Check the USB cable, source and board power circuit.
Both boards must remain running for the range indicator to work. The firmware
keeps brownout protection enabled. If `IN RANGE` is shown but the LED stays off,
check the board's controllable LED pin and `LED_ACTIVE_LOW` setting.

## Software verification

Run these from the project folder:

```sh
python3 test/run_host_tests.py
pio run --environment esp32dev
```

The host tests compile and execute the same `RangeLink` class used by the
firmware, with address and undefined-behavior sanitizers enabled. They require
Python 3 and g++. The 16 scenarios cover a single board, two-board discovery,
blink timing, continuous communication, the timeout boundary, reconnection,
one-direction loss, packet loss, peer and local reboots, equal boot IDs,
invalid/self packets, duplicate/reordered packets, stale/unsent acknowledgements,
clock/sequence rollover and a third board.

These tests verify protocol and LED logic; the PlatformIO build verifies the
ESP32 firmware compiles and fits. Radio reception and the physical D2 LEDs
must be checked on the two boards after upload using the steps above.
