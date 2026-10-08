# DPF Sentinel — ESP32-S3

A small, **read-only** OBD-II logger for the diesel particulate filter (DPF) of a 2014 Hyundai ix35 1.7 CRDi.
It reads soot level, differential pressure, exhaust temperature and regeneration state about once a second
over a BLE ELM327 adapter and **saves everything to a microSD card**. On the screen board it also shows a
colour-coded soot panel. Telegram alerts exist but are off by default.

The goal is a legal, non-invasive way to catch a clogging DPF early and keep regenerations completing —
the opposite of DPF/EGR "delete" tuning, which is illegal for road use and harmful. Not a diagnostic tool;
do not base safety or repair decisions on it. Only the ix35 is verified (see [docs/pids.md](docs/pids.md)).

## Supported hardware

| Role | Part | Env |
|---|---|---|
| OBD adapter | Bluetooth LE ELM327 (Amazon UK B0DQV19QJF) | both |
| Screen logger | CYD ESP32-S3 3.5" ST77922 320x480 touch, speaker, microSD (B0HGQ5XMQ8) | `cyd35` |
| Headless logger | Freenove ESP32-S3 WROOM kit, USB-C, microSD (B0F48DV38M) | `freenove` |
| Optional | DS3231 RTC on I2C ([wiring](docs/wiring.md)) | both |

Details and pins: [docs/hardware.md](docs/hardware.md).

## Quick start

Windows step-by-step: [docs/flashing-windows.md](docs/flashing-windows.md) (macOS/Linux guides to follow).

```bash
git clone https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3.git && cd DPFSentinel-ESP32-S3
cp include/secrets.example.h include/secrets.h     # optional: your WiFi for NTP time
pio run -e cyd35 -t upload                          # or: -e freenove
pio device monitor
```

Insert a FAT32 microSD card. Logs appear in `/obd` as `session_N.csv` (the data) and `raw_N.log`
(every raw adapter reply, for debugging). Plug the adapter into the car and power the board from a
switched USB socket.

## Where is the time from?

No RTC is required. The clock is set from a DS3231 if fitted, else NTP when your WiFi is in range, else from
your phone: join the `DPF-Sentinel` hotspot, open `http://192.168.4.1` and it sets the time. Rows logged
before the clock is known have an empty `unix_time`; `tools/date_session.py` back-fills them from the
`TIME` marker in the raw log. See [docs/log-format.md](docs/log-format.md).

## Layout

```
src/            firmware (obd/ logging/ dpf/ web/ report/ + cyd-only board/ input/ audio/ ui/)
include/        secrets.example.h  (copy to secrets.h, gitignored)
test/           native unit tests built from real captured replies:  pio test -e native
tools/          log helpers (date back-fill, Car Scanner log solver)
docs/           hardware, wiring, log format, PIDs, development, known issues
logs/examples/  a real driving session
```

Licence: GPL-3.0-or-later. See [NOTICE.md](NOTICE.md).
