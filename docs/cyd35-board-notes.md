# 3.5" ESP32-S3 touch board notes (2026-09-26)

Target: Amazon UK B0HGQ5XMQ8 "CYD ESP32 S3 Display ... 3.5 Inch IPS Capacitive Touch 320x480 ST77922 HMI Module, XiaoZhiAI, Audio Speaker Horn".
Factory firmware strings: '3.5_ESP32S3_AP_%d', LVGL. 16MB flash, 8MB PSRAM, SD 4-bit SDMMC, ES8311.

## Verdict
- Best match: LCDWiki / QDtech ES3C35P (ST77922 QSPI). Amazon page itself only confirms "ST77922, 3.5in, 320x480, XiaoZhi, speaker" (https://www.amazon.co.uk/dp/B0HGQ5XMQ8, fetched title only). That the listing is the ES3C35P PCB is an INFERENCE (same chip, same size, same XiaoZhi claim; the same PCB is known on Amazon as Hosyond B0H28X8SQ4 per https://github.com/jlmeredith/ES3C35P). Confirm by probing the real unit.
- The string '3.5_ESP32S3_AP_%d' was NOT found in any fetched source (unverified for every candidate).
- Waveshare 3.5 / 3.5B do NOT match the ST77922 title (ST7796 / AXS15231B) and their SD demos use 1-bit; the SD pins differ from the ES3C35P (see section 2).

## 1. ES3C35P (ST77922) - primary candidate

Sources: [J] https://github.com/jlmeredith/ES3C35P (docs/HARDWARE.md, graded); [X] https://github.com/78/xiaozhi-esp32/tree/main/main/boards/lcdwiki-es3c35p (config.h); [L] https://www.lcdwiki.com/3.5inch_ESP32-S3_Display; [T] https://github.com/tivnantu/esp32-for-fun-docs (01-hardware.md).
"probed" = J's author exercised it on ONE unit. "vendor" = LCDWiki tables only, not exercised.

| Function | GPIO / value | Status (source) |
|---|---|---|
| Display | ST77922, QSPI, 320x480 portrait, RGB565, 40MHz works (ceiling 62.5MHz) | probed [J]; vendor [L] |
| LCD CS / CLK | 10 / 12 | probed [J], agrees [X][L] |
| LCD D0 D1 D2 D3 | 11 / 13 / 14 / 9 | probed [J], agrees [X][L] |
| LCD RST | none; tied to EN/CHIP_PU (X: GPIO_NUM_NC) | probed [J] |
| Backlight | 41, active HIGH, plain GPIO (PWM needs LEDC; dimming untested) | probed [J] |
| TE (tearing) | 42 | from factory firmware decode [J]; not in vendor tables |
| Colour | J: RGB order, invert_colors needed; X: BGR, invert false (different stacks, check on unit) | conflicting, unverified |
| Touch | Sitronix ST7123-compatible regs, I2C 0x55 (not 0x38), up to 5 points | probed [J][T] |
| Touch I2C SDA / SCL | 38 / 39 (shared with ES8311 and expansion header) | probed [J] |
| Touch RST / INT | 48 / 47 | RST probed [J]; INT vendor only [J][L][X] |
| ES8311 codec | I2C 0x18, on same bus 38/39 | probed (bus scan) [J] |
| I2S MCLK / BCLK / LRCLK(WS) | 17 / 18 / 21 | vendor [J], agrees [X][L][T] |
| I2S DOUT (to codec) / DIN (from codec) | 15 / 16 | vendor, untested; one vendor PDF copy reverses them [J] |
| Amp (SC8002B) enable | 1, ACTIVE LOW (X uses pa_inverted) | vendor [J][X] |
| Microphone | on-board, via ES8311; NO ES7210 mentioned in any source | vendor |
| microSD | 4-bit SDIO: CLK 5, CMD 4, D0 6, D1 7, D2 2, D3 3 | vendor [J][L]; untested (D3=GPIO3 is a strapping pin, 10k pull-up [T]) |
| Battery sense | 8 (ADC1_CH7); onboard Li charger | vendor, untested |
| RGB LED (WS2812) | 40 | probed [J] |
| BOOT | 0 | probed [J] |
| UART0 | TX 43 / RX 44 (vendor table has them swapped) | corrected [J][T] |
| Expansion header | 45 / 46 (strapping) | vendor |
| I/O expander | none found in any source | - |
| IMU / RTC | none found (no mention) | - |
| USB | native USB-Serial/JTAG, USB-C, VID 303A PID 1001, no bridge | probed [J] |
| Memory | 16MB QIO flash, 8MB OPI PSRAM 80MHz; FQBN: PSRAM=opi, FlashMode=qio120, USBMode=hwcdc, CPU 240, PartitionScheme app3M_fat9M_16MB | probed / firmware [J] |

Drivers and versions (all from fetched pages):
- ESP-IDF path: esp_lcd_st77922 2.0.2, esp_codec_dev 1.6.2, led_strip 3.0.3, ESP-IDF 5.5.4 (tested baseline) [T]. Vendor init table with use_qspi=1 in [X]; full init sequence in https://github.com/jlmeredith/ES3C35P/blob/main/esphome/st77922-init-sequence.yaml (not fetched in full).
- ESPHome: model CUSTOM + `st7123` touchscreen platform, no external component [J].
- Arduino: vendor examples at https://github.com/ydedox/st77922 (29 examples; libraries/versions NOT read, unverified). Arduino-esp32 issue #12694 (https://github.com/espressif/arduino-esp32/issues/12694) shows no maintained Arduino_GFX/LovyanGFX ST77922 support was reported. LovyanGFX/Arduino_GFX support: UNVERIFIED.
- Landscape: panel ignores MADCTL MX; use MV|MY (0xA0) [J]. xiaozhi port static-asserts no swap-XY [X].

Not verified anywhere: SD pins on real hardware, I2S direction, whether the '3.5_ESP32S3_AP_%d' factory image is the ES3C35P one, whether B0HGQ5XMQ8 is that PCB.

## 2. Waveshare candidates (kept for reference; poor match to ST77922 listing)

Sources: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5 , https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5B , demo code https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5 and ...-3.5B (Arduino/examples raw files). All below from code, none metered.

| Item | 3.5 (ST7796) | 3.5B (AXS15231B) |
|---|---|---|
| Display | ST7796 SPI 320x480; MOSI1 MISO2 SCLK5 DC3 CS none, RST via TCA9554 (0x20) EXIO pin 1 | AXS15231B QSPI; CS12 CLK5 D0..D3 = 1,2,3,4; RST via TCA9554 pin 1 |
| Backlight | GPIO6 | GPIO6 (LEDC in IDF BSP) |
| Touch | FT6336 (FT6X36 lib default addr) | AXS15231B integrated, I2C 0x3B, INT/RST NC |
| I2C | SDA8 SCL7 | SDA8 SCL7 (Arduino gfx demo says Wire.begin(21,22): inconsistent, unverified) |
| ES8311 | I2C addr via ES8311_ADDRRES_0 (value not read); I2S MCLK12 BCLK13 LRCK15 DOUT16 DIN14 | same but MCLK 44 |
| SD | SD_MMC 1-bit in demos: CLK11 CMD10 D0 9 (D1-D3 not given) | same |
| PMU / RTC / IMU | AXP2101 0x34, PCF85063, QMI8658 | same |
| PA enable | not found in fetched code (IDF pa_pin NC in 3.5B) | NC |
| Mic ADC | none, mic via ES8311 | same |
| USB | Type-C | Type-C |
| Demo libs | Arduino_GFX (bundled), TCA9554, TouchDrvFT6X36, XPowersLib, ESP32-audioI2S, LVGL v8; versions not read | LVGL v8/v9 examples; ESP-IDF 01_factory BSP |
The wiki pages did not give pin tables in fetched text; pins come from demo source. Schematic PDFs (e.g. https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5B/ESP32-S3-Touch-LCD-3.5B_V2.0.pdf) were not downloaded.
