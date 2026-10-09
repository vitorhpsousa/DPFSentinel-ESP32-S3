#pragma once
// SETUP: copy this file to include/secrets.h (same folder) and edit the COPY. secrets.h is gitignored,
// so your passwords never get committed. Never paste secrets.h into chats, issues or screenshots.
// Leave a value as "" to disable that feature. Keep the double quotes.

// ---- WiFi (optional) ----------------------------------------------------------------------
// Networks to join for NTP time and the web page. 2.4 GHz only (ESP32 cannot use 5 GHz).
// Example:  #define WIFI_STA_SSID "MyHomeWifi"     #define WIFI_STA_PASS "my wifi password"
// SSID2 is a second network (e.g. your phone hotspot), tried alternately.
#define WIFI_STA_SSID  ""
#define WIFI_STA_PASS  ""
#define WIFI_STA_SSID2 ""
#define WIFI_STA_PASS2 ""

// Password of the logger's own hotspot "DPF-Sentinel" (needs WIFI_AP_ENABLED 1 in config.h).
// At least 8 characters. Change it from the default.
#define WIFI_AP_PASS   "change-me-please"

// ---- Telegram (optional, only used when ENABLE_TELEGRAM is 1 in config.h) -------------------
// Full step-by-step guide in plain English: docs/telegram-setup.md
// 1. In Telegram, message @BotFather, send /newbot, follow the steps. It replies with a TOKEN.
//    The token is the WHOLE string: a number, a colon, then letters/digits. The colon is part of it:
//        TG_BOT_TOKEN  "12345678:AAbbCCddEEffGGhhIIjjKKllMMnnOOppQQr"      <- fake example
// 2. Send any message to your new bot, then open this in a browser (put your token in place of <TOKEN>):
//        https://api.telegram.org/bot<TOKEN>/getUpdates
//    Find  "chat":{"id":123456789  -> that number is your chat id (a different number from the bot's):
//        TG_CHAT_ID    "123456789"                                           <- fake example
// 3. If a token is ever pasted somewhere public, revoke it: @BotFather -> /revoke.
#define TG_BOT_TOKEN   ""
#define TG_CHAT_ID     ""
