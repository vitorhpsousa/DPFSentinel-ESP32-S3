# Factory firmware static analysis (2026-09-26)

File: backup/factory_flash_16MB.bin (read-only analysis). app0 = flash 0x10000, extracted and disassembled with xtensa-esp32s3-elf-objdump (binary wrapped into an ELF with objcopy at VMA 0x42000020).
Addresses: "app0+X" = offset inside the app0 partition; "V" = virtual address. app0 file offset = V - 0x3c0f0020 + 0x20 for the DROM segment, V - 0x42000020 + 0x110020 for the IROM segment, V - 0x3fc97e00 + 0x10066c for .data.

## Image
- PROVEN: app0 is a valid ESP app image (magic E9, 6 segments, entry 0x4037697c): DROM 0x3c0f0020 len 0x100644, DRAM 0x3fc97e00 len 0x6150, IRAM 0x40374000 len 0x9854, IROM 0x42000020 len 0xe883c, IRAM 0x4037d854, RTC 0x600fe100.
- PROVEN: app_desc: project "arduino-lib-builder", built Mar 28 2025 06:13:03, ESP-IDF v5.4.1-1 (arduino-esp32 3.x). Arduino core plus ESP-IDF drivers (LVGL v8, esp_lcd, esp_lcd_touch, i2c_master, i2s_std, esp_codec_dev-style ES8311).
- PROVEN: app1 (0x310000..0x610000) is entirely 0xFF (blank). ffat (0x610000..) is entirely 0xFF (blank, no files). Only app0 is used.
- Strings show the firmware is a small demo: "3.5_ESP32S3_AP_%d" (soft-AP SSID pattern), "i2s_music", "i2s_echo", "i2s es8311 codec example start", "/sdcard", "/SDCard_test.txt", "LVGL porting example", ESP-IDF network_provisioning component.

## Display: ST77922 init table and bus
- PROVEN: st77922_vendor_config_t at app0+0xb3e98 = {init_cmds V=0x3c1a3ec4, init_cmds_size=63, use_qspi=1}. It is referenced by one l32r at V=0x42003fae.
- PROVEN: the 63-entry lcd_init_cmd_t table at app0+0xb3ec4 (16-byte records {cmd, data*, data_bytes, delay_ms}; data blobs in .data at 0x3fc97e00+, zero-filled blobs live in .bss beyond the .data end so read as 0). Decoded and matches the entries in vendor/jlmeredith_ES3C35P_main_esphome_st77922-init-sequence.yaml (F1 00; 60 00 00 00; 65 80; 79 06; 7B 00 08 08; 80 55 62 2F 17 F0 52 70 D2 52 62 EA; ...). Dump (cmd nbytes delay data):
```
 0 F1 1 0 00            | 22 90 9 0 04 04 55 74 00 40 43 2D 2D
 1 60 3 0 00 00 00      | 23 91 9 0 04 04 55 75 00 40 42 2D 2D
 2 65 1 0 80            | 24 92 10 0 04 44 55 C0 06 00 07 05 90 2D
 3 79 1 0 06            | 25 93 10 0 04 43 11 00 00 00 00 05 90 2D
 4 7B 3 0 00 08 08      | 26 94 6 0 00 00 00 00 00 00
 5 80 11 0 55 62 2F 17 F0 52 70 D2 52 62 EA
 6 81 4 0 26 52 72 27   | 27 95 5 0 96 16 00 00 FF
 7 84 2 0 92 25         | 28 96 12 0 44 53 03 12 23 24 06 05 9A 2D 00 44
 8 87 6 0 10 10 58 00 02 3A
 9 88 15 0 00 00 2C 10 04 00 00 00 01 01 01 01 01 00 06
10 89 3 0 00 00 00      | 29 97 12 0 44 53 47 56 20 20 02 01 9A 2D 00 44
11 8A 11 0 13 00 2C 00 00 2C 10 10 00 3E 19
12 8B 9 0 15 B1 B1 44 96 2C 10 97 8E
13 8C 13 0 1D B1 B1 44 96 2C 10 50 0F 01 C5 12 09
14 8D 1 0 0C            | 30 BA 5 0 55 9A 2D 9A 2D
15 8E 6 0 33 01 0C 13 01 01 | 31 9A 7 0 40 00 06 00 00 00 00
16 B3 2 0 00 30         | 32 9B 7 0 00 00 06 00 00 00 00
17 F1 1 0 00            | 33 9C 13 0 5C 12 00 00 10 12 00 00 10 02 00 00 00
18 71 1 0 D0            | 34 9D 8 0 8A 51 00 00 00 80 1E 01
19 66 2 0 02 3F         | 35 9E 7 0 51 00 00 00 80 1E 01
20 BE 3 0 26 00 9D      | 36 B4 12 0 1D 1C 1E 0B 14 02 13 09 1E 00 1E 10
21 70 12 0 01 A6 11 40 E0 00 11 60 11 00 00 1A
37 B5 12 0 1D 1C 1E 0A 15 03 11 08 1E 01 1E 12
38 B6 7 0 77 77 00 0A FF 0A FF
39 86 14 0 C6 04 B1 02 58 12 58 0C 13 01 A5 00 A5 A5
40 B7 16 0 07 0A 0E 06 05 03 2B 03 03 42 07 10 10 2E 3F 0D
41 B8 16 0 07 0A 0D 05 05 02 2B 02 03 42 06 10 0F 2E 3F 0D
42 B9 2 0 23 23  | 43 BF 6 0 10 14 14 0B 0B 0B | 44 F2 1 0 00
45 73 5 0 04 DA 12 54 47 | 46 77 5 0 6B 5B FD C3 C5 | 47 7A 2 0 15 27
48 7B 2 0 04 57 | 49 7E 2 0 01 0E | 50 BF 1 0 36 | 51 E3 2 0 40 40
52 F0 1 0 00 | 53 D0 1 0 00
54 2A 4 0 00 00 01 3F   (CASET 0..319)
55 2B 4 0 00 00 01 DF   (RASET 0..479)
56 21 (INVON) | 57 11 delay 120 (SLPOUT) | 58 29 (DISPON) | 59 2C (RAMWR)
60 3A 1 0 01 (COLMOD) | 61 36 1 0 00 (MADCTL) | 62 35 1 delay 20, data 01 (TEON)
```
  Note: entry 10 (89) and 26 (94) show blank-looking data only because their blobs sit in .bss (all zero); lengths 3 and 6 are proven.
- PROVEN: panel is 320x480 (CASET 0x013F, RASET 0x01DF; LVGL buffer sizing movi 0x140/0x1e0 at V=0x42004394).
- PROVEN: QSPI bus config at V=0x42003f45..0x42003f5e, stores into spi_bus_config_t: data0=11, data1=13, sclk=12, data2=14, data3=9. Matches notes.
- PROVEN: LCD panel-io config at V=0x42003f7f..0x42003fa5: cs_gpio=10, dc_gpio=-1, lcd_cmd_bits=32, lcd_param_bits=8, pclk_hz literal at V=0x42000254 = 0x04C4B400 = 80,000,000 Hz (notes say 40 MHz probed / 62.5 MHz ceiling: the factory firmware runs 80 MHz).
- PROVEN: panel dev config reset_gpio = -1 (movi a7,-1 stored at V=0x42003fe2), bits_per_pixel=16 (V=0x42003fd0). No LCD reset pin (matches notes: tied to CHIP_PU).
- PROVEN: spi_bus max_transfer_sz = 307200 (0x4b<<12 at V=0x42004353) = 320*480*2.
- PROVEN: the ST77922 QSPI vendor init used is the 63-entry vendor table; the driver's default 60-entry table (decoy, 532x300) is not used.
- GUESS: TE pin 42 - a movi 42 is stored into the same config block at V=0x4200436e..0x4200437a (offset +36) next to values 13 and 3, but I did not prove which struct member it is. Matches jlmeredith's claim.

## Backlight
- PROVEN: GPIO 41 driven by LEDC PWM, NOT a plain GPIO. ledc_timer_config at V=0x42004335..0x42004341 (speed mode 0, duty resolution 10, timer 1?, freq 0x1388 = 5000 Hz, offsets consistent with ledc_timer_config_t) and ledc_channel_config with gpio_num=41 at V=0x4200431f. This contradicts board_notes.md ("PWM needs LEDC; dimming untested"): the factory firmware does dim by PWM at 5 kHz, 10-bit.
- GUESS: active-high (not directly determined; a brightness function at V=0x42003ee8 maps 0..100 to duty).

## Touch
- PROVEN: esp_lcd_touch config at V=0x4200419b..0x420041a4: x_max/y_max packed = 0x01E00140 (y 480, x 320), rst_gpio = 48 (movi 48 stored at a1+44), int_gpio = 47 (movi 47 stored at a1+48). Matches notes.
- PROVEN: panel-io I2C config at V=0x420041ce: dev_addr = 85 = 0x55, scl_speed 0x61A80 = 400 kHz (V=0x420000bc). Driver strings are "st77922" touch (Sitronix, error strings "New st77922 failed", "Read max touches failed").

## I2C bus, ES8311
- PROVEN: i2c_master_bus_config at V=0x42002701..0x42002722 (and again at V=0x42003ec3): SDA = 38, SCL = 39, glitch filter 7, internal pullups enabled.
- PROVEN: first i2c device added right after (V=0x42002732): device_address = 24 = 0x18 (16-bit store at a1+40), 400 kHz. This is the ES8311; its register writes go through i2c_master_transmit (function V=0x42003784) with sequential register writes in the es8311 init (function V=0x420037a0). So ES8311 = 0x18 on the same 38/39 bus as touch (0x55).

## I2S
- PROVEN: i2s_std_config_t is copied from rodata V=0x3c107448 (68 bytes memcpy at V=0x420027c6). Its gpio_cfg at V=0x3c107474 = {mclk=17, bclk=18, ws=21, dout=15, din=16} (in IDF's field order mclk,bclk,ws,dout,din). Matches notes exactly, including DOUT=15/DIN=16 direction. The same blob has 16-bit slot, 16000 Hz clock_cfg (16000 seen at V=0x3c107434 area), mclk_multiple 256 (0x100 at V=0x3c10745c). Sample rate 16 kHz and 16-bit width: PROVEN by the constants, mapping of exact field names is GUESS.

## PA enable
- PROVEN: GPIO 1 configured as OUTPUT (pinMode(1,3), V=0x420026e6..0x420026ea) and written LOW (digitalWrite(1,0) at V=0x420026ed..0x420026f1) during setup, and written HIGH (digitalWrite(1,1)) at V=0x420028cc..0x420028d0 after the codec is running (after a wait on BOOT GPIO0, read via digitalRead(0), pinMode(0,INPUT_PULLUP) at V=0x420026df..0x420026e3).
- GUESS: polarity. Firmware sets it LOW at boot and HIGH just before audio playback/echo, which suggests ACTIVE HIGH enable, which contradicts the notes' "active low (pa_inverted in xiaozhi)". Verify on hardware.

## SD card
- PROVEN: SD_MMC.setPins call at V=0x42002fc8..0x42002fdb (Arduino setPins(clk,cmd,d0,d1,d2,d3)): a11=5 (CLK), a12=4 (CMD), a13=6 (D0), a14=7 (D1), a15=2 (D2), stack arg = 3 (D3). Matches notes exactly. A second setPins-style call site exists at V=0x42002f70 region (same values, retry path). Mount point string "/sdcard" and test file "/SDCard_test.txt" exist.
- GUESS: 4-bit mode is used by begin() (mode-1bit argument not decoded); notes say 4-bit.

## Comparison with board_notes.md
All of these are confirmed by the firmware: LCD CS 10, CLK 12, D0..D3 = 11/13/14/9, no LCD RST, touch I2C 38/39 addr 0x55, touch RST 48 / INT 47, ES8311 0x18, I2S 17/18/21/15/16, SD 5/4/6/7/2/3, panel 320x480, init sequence.
Differences/new facts: (1) backlight GPIO 41 is LEDC PWM 5 kHz/10-bit in the factory firmware; (2) LCD SPI clock 80 MHz in factory firmware; (3) I2C at 400 kHz; (4) PA GPIO 1 polarity looks active HIGH in the firmware (unresolved); (5) I2S sample rate 16 kHz, 16-bit, MCLK x256.
Not found in the firmware: battery ADC (GPIO 8), WS2812 (GPIO 40), TE 42 (only a guess); no such code path in this demo.

## Product / vendor strings
Only these name the product: "3.5_ESP32S3_AP_%d", "LVGL porting example", "i2s es8311 codec example start", "ESP32S3_DEV" (Arduino board info), FQBN string "esp32:esp32:esp32s3:...PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,FlashMode=qio120,USBMode=hwcdc". No "Hosyond", "LCDWiki", "QDtech", "XiaoZhi", "Waveshare" or URLs found in app0. NVS holds an older "2.8_ESP32S3_AP" soft-AP name, showing the same vendor software family is shared with a 2.8" board.

## Personal data check (values NOT inspected or copied)
- NVS (0x9000, 0x6000 bytes, 4357 non-0xFF bytes): contains Espressif net80211 WiFi config namespace with keys including ap.ssid, ap.passwd, sta.ssid, sta.pswd, sta.apinfo, cal_data. So WiFi credential-type entries exist (at least the soft-AP definition; whether sta.* hold a real home network was not read). Treat the backup file as potentially sensitive; do not publish it.
- ffat: empty (all 0xFF). app1: empty. otadata not examined.
