# DS3231 RTC (optional)

Without it the clock comes from NTP or your phone each boot. With it the board has the right time immediately,
even with no WiFi and no phone.

| DS3231 | Freenove | CYD 3.5" |
|---|---|---|
| VCC | 3V3 | 3V3 |
| GND | GND | GND |
| SDA | GPIO 47 (set `RTC_SDA_PIN 47`) | GPIO 38 (shared touch bus) |
| SCL | GPIO 48 (set `RTC_SCL_PIN 48`) | GPIO 39 (shared touch bus) |

Address 0x68 (the module's EEPROM at 0x57 is ignored). On the CYD leave `RTC_SDA_PIN/RTC_SCL_PIN` at -1: the
touch driver has already started that I2C bus. The RTC holds UTC; it is rewritten on every NTP/phone sync.
Cell warning: ZS-042-style modules try to charge a non-rechargeable CR2032 — remove the charge resistor/diode
or use a LIR2032. A coin cell in a hot car is also a reason to prefer a module without a charging circuit.
Pin choices for the Freenove are suggestions; confirm they are free on your board before soldering.
