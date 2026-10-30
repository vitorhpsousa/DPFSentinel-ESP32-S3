# DPF Sentinel — ESP32-S3

A small, **read-only** OBD-II logger for the diesel particulate filter (DPF) of a 2014 Hyundai ix35 1.7 CRDi.
It reads soot level, differential pressure, exhaust temperature and regeneration state about once a second
over a BLE ELM327 adapter and **saves everything to a microSD card**. On the screen board it also shows a
colour-coded soot panel. Optional [Telegram alerts](docs/telegram-setup.md) tell you when a regeneration starts and
finishes (on by default on the headless Freenove build, off on the screen builds until you enable them). A web page
(`http://192.168.4.1` on the board's hotspot, or `http://obd-logger.local` on your Wi-Fi) shows live tiles, the next-regen
estimate and dash mileage, the last 2 minutes as charts, and the log files to download.

The goal is a legal, non-invasive way to catch a clogging DPF early and keep regenerations completing —
the opposite of DPF/EGR "delete" tuning, which is illegal for road use and harmful. Not a diagnostic tool;
do not base safety or repair decisions on it. Only the ix35 is verified (see [docs/pids.md](docs/pids.md)).

**No board?** The [Android app](android/README.md) does the same job from your phone: it talks to the same
BLE adapter, writes the same logs, sends the same Telegram alerts, and shows soot and the next regen on
**Android Auto**.

## Supported hardware

| Role | Part | Env |
|---|---|---|
| OBD adapter | Bluetooth LE ELM327 (Amazon UK B0DQV19QJF) | both |
| Screen logger | CYD ESP32-S3 3.5" ST77922 320x480 touch, speaker, microSD (B0HGQ5XMQ8) | `cyd35` (portrait), `cyd35_flipped`, `cyd35_landscape` |
| Headless logger | Freenove ESP32-S3 WROOM kit, USB-C, microSD (B0F48DV38M) | `freenove` |
| Optional | DS3231 RTC on I2C ([wiring](docs/wiring.md)) | both |

Details and pins: [docs/hardware.md](docs/hardware.md).

The screen is portrait by default (short edge at the top, USB-C at the bottom). `cyd35_flipped` turns it
180°, `cyd35_landscape` keeps the older landscape layout; the setting is `BOARD_ROTATION` in
`src/board/board.h`. The main screen shows soot, an estimate of the miles to the next regen (from the
soot rate, counting down to the level this car's regens start at; an estimate, not an ECU value; below
12 g of soot it shows "not soon", because replayed over logged cycles it was typically 30+ mi off there),
and the live values. The web page can also set a dash-vs-ECU mileage offset: [docs/odometer-offset.md](docs/odometer-offset.md).

## Quick start

Full step-by-step guides for Windows, macOS and Linux: [docs/deploy-cyd35.md](docs/deploy-cyd35.md) (screen board) and
[docs/deploy-freenove.md](docs/deploy-freenove.md) (headless board). Shorter Windows version: [docs/flashing-windows.md](docs/flashing-windows.md).

```bash
git clone https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3.git && cd DPFSentinel-ESP32-S3
cp include/secrets.example.h include/secrets.h     # optional: your WiFi for NTP time
pio run -e cyd35 -t upload                          # or: -e cyd35_flipped / cyd35_landscape / freenove
pio device monitor
```

Insert a FAT32 microSD card. Logs appear in `/obd` as `session_N.csv` (the data) and `raw_N.log`
(every raw adapter reply, for debugging). Plug the adapter into the car and power the board from a
switched USB socket.

## Where is the time from?

No RTC is required. The clock is set from a DS3231 if fitted, else NTP when your WiFi is in range, else from
your phone: join the `DPF-Sentinel` hotspot (password `change-me-please` unless you set `WIFI_AP_PASS` in
`include/secrets.h`; change it, and it is also printed on serial at boot), open `http://192.168.4.1` and it sets the time. Rows logged
before the clock is known have an empty `unix_time`; `tools/date_session.py` back-fills them from the
`TIME` marker in the raw log. See [docs/log-format.md](docs/log-format.md).

## Dash mileage doesn't match?

Normal on several cars — the dash and the ECU's own odometer are often two different counters. Set a
one-time offset with no reflash needed: [docs/odometer-offset.md](docs/odometer-offset.md).

## Layout

```
src/            firmware (obd/ logging/ dpf/ web/ report/ + cyd-only board/ input/ audio/ ui/)
include/        secrets.example.h  (copy to secrets.h, gitignored)
test/           native unit tests built from real captured replies:  pio test -e native
android/        phone + Android Auto app (Kotlin): same PIDs, logs and alerts, no board needed
tools/          log helpers (date back-fill, fetch logs over Wi-Fi), other-car discovery + Car Scanner solver, export, fonts
docs/           hardware, wiring, log format, PIDs, development, known issues
logs/examples/  a real driving session
```

## Docs

- Step-by-step install: [CYD screen board](docs/deploy-cyd35.md) · [Freenove headless board](docs/deploy-freenove.md) · [Windows quick flash](docs/flashing-windows.md)
- [Hardware](docs/hardware.md) · [Wiring (optional RTC)](docs/wiring.md) · [Telegram alerts](docs/telegram-setup.md) · [Dash mileage offset](docs/odometer-offset.md)
- [Log format](docs/log-format.md) · [Verified PIDs and decoding](docs/pids.md) · [ix35 known issues](docs/ix35-known-issues.md)
- [Adapting to another car](docs/other-cars.md) · [Tools](tools/README.md) · [Development](docs/development.md) · [Contributing](CONTRIBUTING.md)
- [Disclaimer](docs/disclaimer.md)

If this saves you a DPF job, you can support the project on [Ko-fi](https://ko-fi.com/vitoi).

Licence: GPL-3.0-or-later. See [NOTICE.md](NOTICE.md).
