# CYD-OBDII — Diesel Diagnostics Dash

A touch-screen gauge display for a **2005 Ford Excursion 6.0L Powerstroke**, built on the
ESP32 "Cheap Yellow Display" (CYD). It talks to the truck through a Bluetooth ELM327 OBD-II
adapter and shows live engine, transmission and ABS data on swipeable pages of gauges,
plus a list of stored trouble codes.

Without an adapter it runs on demo values, so you can test the screens on the bench.

| Page 1: gauges | Page 3: ABS |
|---|---|
| ![Page 1 with transfer case, turbo boost, speed and trans temp](docs/images/page1-gauges.jpg) | ![ABS page with wheel speeds and ABS warning card](docs/images/page3-abs.jpg) |
| **Full-screen gauge** | **Page 4: stored error codes** |
| ![Turbo boost shown full screen](docs/images/fullscreen-turbo-boost.jpg) | ![Stored error codes page listing P0470 and P2291](docs/images/page4-error-codes.jpg) |

*The photos show demo mode (adapter not connected), so all the values are sample data.*

> **Help wanted:** some values still need the right Ford PIDs. See the
> [open issues](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22) if you know your way
> around 6.0L / Super Duty modules.

---

## Features

- **4 pages**, tap to move between them (page dots at the bottom show where you are)
  1. Transfer case · Turbo boost · Vehicle speed · Transmission temp
  2. FICM main power · Coolant temp · System volts · Engine RPM
  3. ABS: Left-front wheel speed · Right-front wheel speed · Rear axle speed · ABS status (warning triangle when the ABS module has stored codes)
  4. Engine trouble codes (up to 15, e.g. `P0470`): stored codes in red, then pending codes in
     yellow (detected once but not confirmed yet, so they show up before the check-engine light);
     green when clear
- **Tap menu**: tap any gauge to get **NEXT PAGE** and **FULL SCREEN** buttons.
  Full screen shows that one gauge as large as it fits; tap again to go back.
- **Gauge alarms**: a gauge that goes out of range (low FICM voltage, hot transmission or
  coolant, low or high system voltage) flashes red, and the status bar shows it on every page,
  see [Gauge alarms](#gauge-alarms).
- **Status bar** that never scrolls: connection state, demo mode, Bluetooth retry
  countdown, gauge alarms, a red warning when engine trouble codes are stored and a yellow one
  for pending codes.
- **Auto brightness**: the CYD's light sensor dims the screen at night and brings it back to full
  brightness in daylight, see [Auto brightness](#auto-brightness).
- **Battery protection**: the screen turns off when the truck is off, then the display sleeps
  and checks every few minutes whether the truck is on, see [Battery protection](#battery-protection).
- **Demo mode**: runs on sample values whenever the adapter isn't connected, and keeps retrying
  the connection in the background so the screen and touch never freeze.
- **Fast polling**: only the values on screen are read from the truck, and every
  request waits for the adapter's `>` prompt, so readings never get mixed up.

## Hardware

| Part | Notes |
|---|---|
| ESP32-2432S028R ("Cheap Yellow Display") | 2.8" 320×240 ILI9341 screen, XPT2046 resistive touch |
| Bluetooth Classic ELM327 OBD-II adapter | Tested with an ELM327 v1.4b clone. Must be **Bluetooth Classic** (SPP), not BLE-only. |
| 3D-printed case | [Modular CYD case](https://www.thingiverse.com/thing:6991968) by DanielBrunod, remixed from [ncmreynolds' CYD enclosure](https://www.thingiverse.com/thing:6308391) (CC BY-SA). STL files in [`hardware/case/`](hardware/case/) |
| USB power in the truck | 5 V via the CYD's USB port |

No wiring is needed: the screen and touch are built into the CYD, and the adapter plugs
into the truck's OBD-II port under the dash.

## Vehicle / bus facts

These were confirmed on the truck with a PC and [`tools/obd_logger.py`](tools/) (see
[PC logging tool](#pc-logging-tool)):

- Protocol: **ISO 15765-4 CAN, 11-bit, 500 kbps** (ELM protocol 6, `ATSP6`), *not* J1850 PWM
- Engine PCM: requests to **7E0**, replies from **7E8**. A second module (7E9, likely the
  transmission) also answers standard requests.
- Standard PIDs confirmed working: `0104 0105 010B 010C 010D 010F 0110 011F 012F 0133 0142`
- Ford-specific FICM PID `2209D0` answers; the raw value is volts × 256
  (e.g. `0x2F80` → 47.5 V)

## Where each value comes from

| Gauge | Module / header | Request | Formula | Status |
|---|---|---|---|---|
| Turbo boost | PCM `7E0` | `010B` MAP, `0133` baro | (MAP − baro) kPa × 0.145 → psi, clamped at 0 | ✅ Confirmed |
| Vehicle speed | PCM `7E0` | `010D` | A km/h × 0.621 → mph | ✅ Confirmed |
| Engine RPM | PCM `7E0` | `010C` | (A×256 + B) / 4 | ✅ Confirmed |
| System volts | PCM `7E0` | `0142` | (A×256 + B) / 1000 | ✅ Confirmed |
| FICM main power | PCM `7E0` | `2209D0` | raw / 256 V | ✅ Confirmed |
| Coolant temp | PCM `7E0` | `221139` | raw / 256 °C → °F | ⚠️ Not yet confirmed |
| Trans temp | PCM `7E0` | `221E1C` | raw / 8 °F | ⚠️ Not yet confirmed |
| Engine trouble codes | PCM `7E0` | `03` | CAN Mode 03, multi-frame aware | ⚠️ Not yet confirmed |
| Pending trouble codes | PCM `7E0` | `07` | CAN Mode 07, same format as Mode 03 | ⚠️ Not yet confirmed |
| Transfer case | `418F02` | `2211B2` | 1 = 2H, 2 = 4H, 4 = 4L | ❌ Returns NO DATA; real module address needed |
| Wheel speeds (LF/RF/rear) | ABS `760` | placeholders | placeholder scale | ❌ Placeholders; get real PIDs from FORScan |
| ABS trouble codes | ABS `760` | `1800FF00` | KWP service 18 count | ⚠️ Not yet confirmed |

Turbo boost uses standard PID `0B`, which tops out at 255 kPa, so the gauge maxes out at about 22 psi of boost.

## Getting started

### 1. Tools
- [VS Code](https://code.visualstudio.com/) with the **PlatformIO** (or pioarduino) extension
- Libraries are installed automatically from `platformio.ini`:
  `TFT_eSPI 2.5.43`, `ELMDuino 3.4.1`, `XPT2046_Touchscreen`

### 2. Set your adapter's Bluetooth address
In [`src/config.h`](src/config.h), replace the placeholder MAC with your adapter's address:

```cpp
const uint8_t ELM_MAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};
```

You can find the address by pairing the adapter with a phone or PC and checking its Bluetooth
details. The ESP32 connects with PIN `1234` (set in `src/main.cpp`).

### 3. Build and upload
Plug in the CYD over USB, then in PlatformIO: **Build**, then **Upload**. From a terminal:

```bash
pio run -t upload
pio device monitor        # 115200 baud
```

### 4. In the truck
Plug in the adapter, turn the key on (engine running for the best data) and power the CYD.
The status bar shows `BT OFFLINE - CONNECTING...` until the link is up, then
`MONITOR ACTIVE - TAP TO SWIPE`.

## Gauge alarms

A gauge whose value goes outside its limits gets a red frame and flashes solid red, and the
status bar turns red with the reading, e.g. `[!] TRANS TEMP HIGH: 224 F`, on whatever page is
showing. Gauges with limits are also read in the background every 2 seconds while they're on
another page, so an alarm is never missed. Each alarm and its clearing are logged to serial
(`ALARM` lines). Alarms only use live readings, never demo data.

| Gauge | Alarm when | Setting in [`src/config.h`](src/config.h) |
|---|---|---|
| FICM main power | below 45.0 V | `ALARM_FICM_LOW_V` |
| Trans temp | above 220 °F | `ALARM_TFT_HIGH_F` |
| Coolant temp | above 230 °F | `ALARM_ECT_HIGH_F` |
| System volts | below 11.8 V or above 15.0 V | `ALARM_VOLTS_LOW_V`, `ALARM_VOLTS_HIGH_V` |

To change a limit, edit its value in `config.h` and upload again. To add a limit to another gauge,
or remove one, edit the last two columns (low limit, high limit) of its row in the `metrics[]`
table in [`src/display_ui.h`](src/display_ui.h); `NO_ALARM` means no limit. For example, to warn
above 3,200 RPM:

```cpp
{"ENGINE RPM",     &valRPM,    "",    TFT_WHITE,   pollRPM,    NO_ALARM, 3200},
```

`ALARM_CHECK_INTERVAL_MS` (2000) sets how often off-screen gauges are checked, and
`ALARM_BLINK_MS` (500) the flash speed.

## Auto brightness

The light sensor on the front of the CYD (an LDR on GPIO34) sets the backlight brightness.

- On this board the sensor reads **0 in normal room light or brighter**, rises to 100–350 in
  shade and to about 2,600 in full darkness (higher = darker).
- 0 gives 100% brightness; a reading of 2,000 or more gives the 10% night floor. The curve is
  squared, so dusk and shade only dim the screen a little and it drops off near full darkness.
- The reading is smoothed over about 3 seconds, so a passing shadow or headlights don't make the
  screen flicker.
### Changing the brightness levels

All the settings are in the "Auto brightness" block of [`src/config.h`](src/config.h). After
changing one, upload again (`pio run -t upload`).

| Setting | Default | What it does |
|---|---|---|
| `AUTO_BRIGHTNESS` | `true` | `false` turns auto brightness off: the screen is always at 100% |
| `BRIGHTNESS_MIN_PCT` | `10` | Brightness in full darkness, in % (1–100) |
| `LDR_DARK_RAW` | `2000` | Sensor reading at which the screen reaches `BRIGHTNESS_MIN_PCT` |
| `LDR_BRIGHT_RAW` | `0` | Sensor reading at which the screen is at 100% |
| `LIGHT_SMOOTHING_MS` | `3000` | How quickly the brightness follows the light, in ms (higher = slower and steadier) |
| `LIGHT_LOG_INTERVAL_MS` | `0` | How often to log sensor readings, in ms (`0` = off; use `500` for calibration) |

Common adjustments:

- **Too dim at night / too bright at night:** raise / lower `BRIGHTNESS_MIN_PCT`, e.g. `20` or
  `5`.
- **Dims too early (at dusk or in shade):** raise `LDR_DARK_RAW`, e.g. `2400`; the screen then
  only dims in deeper darkness.
- **Doesn't dim enough at night:** lower `LDR_DARK_RAW`, e.g. `1000`, so a less dark cab already
  counts as full darkness.
- **Brightness hunts or flickers** (streetlights, shadows): raise `LIGHT_SMOOTHING_MS`, e.g.
  `6000`. **Reacts too slowly** (tunnels): lower it, e.g. `1500`.

### Recalibrating for your board

The sensor's range differs between CYD boards, so if the defaults don't suit yours:

1. In [`src/config.h`](src/config.h), set `LIGHT_LOG_INTERVAL_MS = 500` and upload.
2. Open the serial monitor (`pio device monitor`). Twice a second it logs a line like
   `(LIGHT): LDR raw 2587, smoothed 2555 -> backlight 10%`. `raw` is the sensor reading.
3. Note the `raw` value in each lighting condition you care about: daylight, the cab at dusk,
   the cab at night with the dash lights on, and fully covered.
4. Set `LDR_BRIGHT_RAW` to the reading where you still want 100% (usually `0`) and
   `LDR_DARK_RAW` to the reading where you want the night level.
5. Set `LIGHT_LOG_INTERVAL_MS` back to `0` and upload again.

On the board this was developed on, the readings were 0 in room light, 100–350 in shade and
about 2,600 fully dark. Higher always means darker on the CYD.

## Battery protection

The OBD port's power pin is live with the key off, so the display powers itself down when the
truck goes quiet:

1. After **2 minutes** with no activity the backlight turns off. A tap turns it back on (that
   tap doesn't press anything on screen).
2. After **5 minutes** the ESP32 goes into **deep sleep** with the screen off.
3. It wakes on a **tap**, and also every **5 minutes** for a timed check: it starts with the
   screen still off and tries the adapter for 30 seconds. If the PCM answers (key on) the screen
   comes on; if not, it goes back to sleep. So after you start the truck the screen comes on by
   itself within about 5 minutes, or straight away if you tap it.

"Activity" means **the PCM answering a request**, or a tap. The Bluetooth link alone doesn't
count, because the adapter is powered from the OBD port and stays connected with the key off.
On the bench with no adapter, the display sleeps after 5 minutes too; tap to wake it.

| Setting | Default | What it does |
|---|---|---|
| `SCREEN_OFF_AFTER_MS` | 120000 | Backlight off after this long with no activity (0 = never) |
| `SLEEP_AFTER_MS` | 300000 | Deep sleep after this long with no activity (0 = never) |
| `WAKE_CHECK_INTERVAL_MS` | 300000 | How often a sleeping display wakes to check the truck (0 = only wake on a tap) |
| `WAKE_CHECK_MS` | 30000 | How long each check tries the adapter before sleeping again |

The ELM327 adapter itself keeps drawing current from the OBD port the whole time (typically
tens of mA), whatever the display does. Unplug it, or use one with an auto-sleep mode, if the
truck sits for long periods. The CYD board also draws a few mA in deep sleep (regulator,
USB-serial chip, power LED), plus about 30 s awake every 5 minutes for the checks.

## Configuration

Everything you're likely to change is in [`src/config.h`](src/config.h):

| Setting | Default | What it does |
|---|---|---|
| `ELM_MAC` | placeholder | Bluetooth address of your ELM327 |
| `POLL_INTERVAL_MS` | 250 | How often the visible values are refreshed |
| `DTC_SCAN_INTERVAL_MS` | 10000 | How often engine codes are re-checked when not on the codes page |
| `BT_RETRY_INTERVAL_MS` | 10000 | Wait between Bluetooth connection attempts |
| `MENU_TIMEOUT_MS` | 4000 | How long the tap menu stays up |
| `ALARM_*` | see [Gauge alarms](#gauge-alarms) | Alarm limits for FICM, trans temp, coolant and system volts |
| `AUTO_BRIGHTNESS` | true | Set the backlight from the light sensor (false = always full brightness) |
| `BRIGHTNESS_MIN_PCT` | 10 | Night-time backlight level |
| `LDR_DARK_RAW` / `LDR_BRIGHT_RAW` | 2000 / 0 | Sensor readings for the night floor / full brightness |
| `SCREEN_OFF_AFTER_MS` / `SLEEP_AFTER_MS` | 120000 / 300000 | Screen off / deep sleep after this long with no activity |
| `WAKE_CHECK_INTERVAL_MS` / `WAKE_CHECK_MS` | 300000 / 30000 | Timed wake-up checks while asleep |
| `TOUCH_RAW_*` | 200–3700 / 240–3800 | Touch calibration (the defaults work on the tested board) |

**Adding or moving a gauge:** edit the `metrics[]` table in
[`src/display_ui.h`](src/display_ui.h). Each row is a label, value, unit, colour and the
function that reads it, and the pages are filled 4 at a time in table order. Only gauges on
screen are polled, so the table is the only place you need to change.

## Project structure

```
CYD-OBDII/
├── platformio.ini       Board, display pin setup (TFT_eSPI build flags), libraries
├── src/
│   ├── config.h         Pins, tuning constants, adapter MAC, shared declarations
│   ├── globals.cpp      Shared objects and values, demo values
│   ├── main.cpp         setup() / loop(): touch, refresh timing, connect/disconnect handling
│   ├── obd_engine.h     ELM327 communication, PID decoding, DTC reading, Bluetooth task
│   ├── display_ui.h     Gauge table, alarms, cards, full-screen gauge, tap menu, status bar, pages
│   ├── brightness.h     Auto brightness: light sensor -> backlight PWM
│   └── power.h          Battery protection: screen off, deep sleep, wake on a tap or timed check
├── hardware/
│   └── case/            3D-printable case (STL files, credits, build notes)
├── tools/
│   └── obd_logger.py    PC script that surveys the truck through a USB ELM327 and writes a log
├── docs/                Reference captures from the truck
└── test/
    └── serialLog.txt    Serial log captured in the truck (used to fix reply timing and FICM scaling)
```

## PC logging tool

[`tools/obd_logger.py`](tools/) is a Python script for a PC with a USB ELM327 adapter. It finds
the protocol and every module that answers, lists each module's supported PIDs, reads and
decodes them, measures response times, and writes everything, including a raw transcript, to one
text file. Use the log to choose the PIDs and formulas for the firmware. It only sends read requests.

```bash
pip install pyserial
python tools/obd_logger.py --note "engine idling, warm"
```

See [`tools/README.md`](tools/README.md) for the options and how to add Ford-specific PIDs.

## How it works

- **Two cores:** Bluetooth connection attempts run in a background FreeRTOS task on
  core 0. Drawing, touch and polling run in `loop()` on core 1, so a slow connect never
  freezes the screen.
- **Request/reply sync:** each command is sent and the reply is read up to the ELM `>`
  prompt before the next one goes out. `ATSH` (set header) is only sent when the target
  module changes. Fixed delays turned out to make replies arrive one request late over
  Bluetooth.
- **Drawing:** the gauge area is a 320×214 8-bit sprite (about 67 KB) drawn off-screen and
  pushed in one go, so updates don't flicker. Page changes slide horizontally.
- **Debug log:** everything is logged over USB serial at 115200 with timestamps, e.g.
  `010C -> 410C0AD6` or `Page 0 refreshed in 380 ms`.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Stuck on `BT OFFLINE - CONNECTING...` | Wrong `ELM_MAC`, adapter not powered (key off), or a BLE-only adapter |
| `BT STACK FAILED - DEMO DATA` | ESP32 Bluetooth failed to start; power-cycle the CYD |
| A gauge stays at `---` | The truck doesn't answer that PID; check the serial log for `no valid reply` |
| Taps hit the wrong gauge | Recalibrate `TOUCH_RAW_*` using the `Tap -> RawX/RawY` lines in the serial log |
| Readings look swapped between gauges | Replies are out of sync; check for `Timeout waiting for '>'` in the log |

## Roadmap

- [ ] Find the transfer case module and its address with FORScan (it may be on MS-CAN)
- [ ] Real ABS wheel-speed PIDs and scaling
- [ ] Confirm coolant temp, trans temp and DTC reads in a new truck log
- [ ] Hard-wire the CYD to the CAN bus instead of using the Bluetooth ELM327 adapter: in progress
      in [CYD-OBDII-CAN](https://github.com/cbrochar/CYD-OBDII-CAN) (TWAI + SN65HVD230), which also
      has a CAN sniffer with SD card recording
- [ ] Measure the key-off current draw (screen-off, deep sleep and timed wake-up checks are done
      and tested on the bench; the ELM327 adapter draws current on its own as well)

## Contributing

Ideas, bug reports and pull requests are welcome.

- **Know a Ford PID or module address?** The open [`help wanted`](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22)
  issues list what's still unknown (transfer case, ABS wheel speeds, temperature PIDs). Log
  output from [`tools/obd_logger.py`](tools/) or FORScan is the most useful thing you can add.
- **Code feedback:** open an issue or start a thread in Discussions. The ELM327 reply handling in
  [`src/obd_engine.h`](src/obd_engine.h) and the drawing code in
  [`src/display_ui.h`](src/display_ui.h) are good places to start.
- Please only add **read-only** requests (see Safety below).

## Safety

This project only **reads** data (services `01`, `03`, `07`, `22`, `18`). Don't add requests that
command modules (services `2E`, `2F`, `31`, `10`), since those can actuate hardware or change
settings. Mount the display where it doesn't block your view, and don't operate it while
driving.

## License

The code is released under the [MIT License](LICENSE). The 3D-printable case in
[`hardware/case/`](hardware/case/) is a remix of designs by DanielBrunod and ncmreynolds and stays
under **CC BY-SA**. See [hardware/case/README.md](hardware/case/README.md).
