# Notices
Copyright the DPF Sentinel authors. Licensed GPL-3.0-or-later (see LICENSE).

The CYD 3.5" board support is original code written from facts derived from that board's own factory firmware and
live probing. Pin/register facts were cross-checked against public work (not copied): jlmeredith/ES3C35P (MIT),
78/xiaozhi-esp32 (MIT), ESPHome `st7123` touchscreen component (GPL-3.0), Espressif's ES8311 reference driver.
Fonts in tools/fonts: DejaVu (free licence, see tools/fonts/README). The Telegram root certificate is a public
CA certificate.

## Libraries compiled into the firmware

The firmware compiles against the third-party code below (from `platformio.ini`), each under its own licence. None of
this changes the licence of the code written for this project.

- **NimBLE-Arduino** (h2zero, `^1.4.3`, all builds): BLE client. Apache License 2.0 (the package's own `LICENSE`).
  https://github.com/h2zero/NimBLE-Arduino
- **Adafruit GFX Library** (`^1.11.9`, `cyd35*` builds only): drawing on the screen. BSD licence, Copyright (c) 2012
  Adafruit Industries (the package's own `license.txt`). https://github.com/adafruit/Adafruit-GFX-Library
- **Adafruit BusIO** (pulled in automatically by Adafruit GFX, `cyd35*` builds only): MIT licence, Copyright (c) 2017
  Adafruit Industries (the package's own `LICENSE`). https://github.com/adafruit/Adafruit_BusIO
- **Arduino-ESP32 core** (`framework = arduino` on `platform = espressif32@7.1.3`, which installs core 2.0.17; all
  builds): WiFi, web server, SD/LittleFS, `Preferences` and the rest of the Arduino layer. LGPL-2.1-or-later per the
  project's repository; ESP-IDF underneath is Apache-2.0. https://github.com/espressif/arduino-esp32

The Unity test framework is used only by `pio test -e native` on your computer and is not compiled into the firmware.

### If you distribute a compiled `.bin`

This project is source-only: everyone builds their own firmware with their own secrets. If you ever share a compiled
binary, the terms above travel with it: NimBLE-Arduino and ESP-IDF are Apache-2.0 and Adafruit GFX/BusIO are BSD/MIT
(keep their attribution); the Arduino-ESP32 core is LGPL-2.1-or-later, which requires making the core's source (or
your changes to it) available. Distributing the stock, unmodified core as installed by PlatformIO satisfies this.
