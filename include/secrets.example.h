#pragma once
// Copy to include/secrets.h (gitignored) and fill in. Leave a value empty to disable that feature.

// Home / phone-hotspot networks to join (2.4 GHz only). Used for NTP time and the web page.
#define WIFI_STA_SSID  ""
#define WIFI_STA_PASS  ""
#define WIFI_STA_SSID2 ""
#define WIFI_STA_PASS2 ""

// Own hotspot (only used when WIFI_AP_ENABLED=1 in config.h). Min 8 characters.
#define WIFI_AP_PASS   "change-me-please"

// Telegram (only compiled when ENABLE_TELEGRAM=1 in config.h): bot token from @BotFather
// and your chat id (message.chat.id from api.telegram.org/bot<TOKEN>/getUpdates).
#define TG_BOT_TOKEN   ""
#define TG_CHAT_ID     ""
