#pragma once
// Tunables for DPF Sentinel. Secrets (WiFi passwords, Telegram) live in include/secrets.h.
// Board selection comes from the PlatformIO env (-D BOARD_CYD35=1 or -D BOARD_FREENOVE=1).

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_STA_SSID  ""
#define WIFI_STA_PASS  ""
#define WIFI_STA_SSID2 ""
#define WIFI_STA_PASS2 ""
#define WIFI_AP_PASS   "change-me-please"
#define TG_BOT_TOKEN   ""
#define TG_CHAT_ID     ""
#endif

#if defined(BOARD_CYD35)
  #define HAS_DISPLAY 1
  // SD: SD_MMC 4-bit (falls back to 1-bit)
  #define SDMMC_CLK_PIN 5
  #define SDMMC_CMD_PIN 4
  #define SDMMC_D0_PIN  6
  #define SDMMC_D1_PIN  7
  #define SDMMC_D2_PIN  2
  #define SDMMC_D3_PIN  3
  #define AUDIO_ENABLED 1
  #define BOOT_JINGLE 1          // 0 soft chirp, 1 Rondo alla Turca, 2 Eine kleine Nachtmusik
#elif defined(BOARD_FREENOVE)
  #define HAS_DISPLAY 0
  // Freenove ESP32-S3-WROOM CAM board: SD_MMC 1-bit on 38/39/40 (D1-D3 unused).
  // UNVERIFIED on this unit: check docs/hardware.md before first flash.
  #define SDMMC_CLK_PIN 39
  #define SDMMC_CMD_PIN 38
  #define SDMMC_D0_PIN  40
  #define SDMMC_D1_PIN  -1
  #define SDMMC_D2_PIN  -1
  #define SDMMC_D3_PIN  -1
  #define AUDIO_ENABLED 0
#else
  #error "Select a board: build with env cyd35 or freenove"
#endif

// --- OBD adapter (BLE ELM327) ---
// Discovery aids: enable at most ONE. 1: list BLE devices in range. 2: list GATT services of the adapter.
#define BLE_SCAN_ONLY 0
#define BLE_DUMP_SERVICES 0
// Leave empty to auto-detect (names containing OBD/ELM/VLINK/VEEPEAK/IOS-). Pin the address once known.
#define BLE_TARGET_ADDRESS ""
#define BLE_NAME_HINT ""
// GATT UART UUIDs: empty = auto-detect (Nordic UART, then any notify+write pair); printed at connect.
#define BLE_SERVICE_UUID ""
#define BLE_CHAR_TX_UUID ""   // write: ESP32 -> adapter
#define BLE_CHAR_RX_UUID ""   // notify: adapter -> ESP32

// --- Storage ---
// 1 = microSD (files in /obd, nothing else on the card is touched). 0 = internal flash (~30 min only).
#define USE_SD 1
// 1 = ask the card for free space (walks the FAT, can be slow on big cards).
#define SD_FREE_CHECK 0

// --- Clock (no RTC needed) ---
// Time is set from NTP (when WiFi joins) or the phone via the web page; rows before that carry a
// blank unix_time and a TIME marker in the raw log lets tools/date_session.py back-fill them.
// A DS3231 on I2C (addr 0x68) is used automatically if present (see docs/wiring.md).
#define NTP_ENABLED 1
#define RTC_DS3231_ENABLED 1
#define RTC_SDA_PIN -1          // -1 = board default (CYD: shares the touch bus 38/39); Freenove: set e.g. 47
#define RTC_SCL_PIN -1
// POSIX time-zone rule for local time (UK default; DST automatic). Stored log times are always UTC.
#define TZ_RULE "GMT0BST,M3.5.0/1,M10.5.0"

// --- WiFi + small web page (live values, log download/delete, set time) ---
#define WEB_ENABLED 1
#define WIFI_AP_ENABLED 1       // host the "DPF-Sentinel" hotspot; join it and open http://192.168.4.1
#define WIFI_AP_SSID "DPF-Sentinel"
#define WIFI_DISABLED 0         // diagnostic: 1 = never start WiFi

// --- Telegram (OFF by default; needs include/secrets.h token + chat id) ---
#define ENABLE_TELEGRAM 0
#define TG_ALERT_WIFI 1
#define TG_ALERT_REGEN 1        // regen start/finish alerts (also drives the on-device regen beeps)
#define TG_SEND_HOUR 17
#define TG_SEND_MIN 30
#define TG_SEND_RAW 1
#define TG_MIN_BYTES 3000
#define TG_STATUS_ENABLED 0
#define TG_STATUS_INTERVAL_MIN 5
#define TG_STATUS_MIN_COOLANT_C 80.0f
#define TG_HOST "api.telegram.org"
#define TG_PORT 443
#define TG_USE_TLS 1

// --- Behaviour ---
#define ROW_INTERVAL_MS 1000       // one CSV row per second (latest values held)
#define SERIAL_RAW_ECHO 0          // 1 = print every raw adapter exchange (bring-up)
#define BLANK_ROWS_REINIT 10       // all-blank rows before re-initialising the adapter
#define BLANK_ROWS_RECONNECT 30    // ... before a full BLE reconnect

// DPF idle-blockage heuristic (differential pressure at idle) — threshold unverified, see docs.
#define DPF_IDLE_PRESSURE_THRESHOLD_HPA 20.0f
#define DPF_IDLE_RPM_CEILING            1100

// Optional status LEDs (-1 = none). Free GPIOs on the Freenove board: 1 and 2.
#define LED_REGEN_PIN -1
#define LED_TEMP_WARN_PIN -1
#define DPF_TEMP_WARN_THRESHOLD_C 300.0f
