#include "../config.h"
#if ENABLE_TELEGRAM
#include "telegram_report.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "../config.h"
#include "../web/timekeeper.h"
#include "../web/web_ui.h"
#include "tg_root_ca.h"

static SessionStore *gStore = nullptr;
static uint32_t gBoot = 0;
static volatile bool gForce = false;
static String gLastResult = "not run yet";
static uint32_t gRetryAfter = 0;

static String urlEncode(const String &s) {
    String o;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

static bool readReply(Client &c, String &body) {
    uint32_t t0 = millis();
    while (!c.available() && c.connected() && millis() - t0 < 20000) delay(20);
    String all;
    while ((c.available() || c.connected()) && millis() - t0 < 25000) {
        while (c.available()) all += (char)c.read();
        if (all.indexOf("\r\n\r\n") >= 0 && !c.connected()) break;
        delay(10);
    }
    body = all;
    return all.startsWith("HTTP/1.1 200") && (all.indexOf("\"ok\":true") >= 0 || all.indexOf("\"ok\": true") >= 0);
}

static bool openConn(WiFiClientSecure &tls, WiFiClient &plain, Client *&out) {
#if TG_USE_TLS
    tls.setCACert(TG_ROOT_CA);
    tls.setTimeout(15);
    if (!tls.connect(TG_HOST, TG_PORT)) return false;
    out = &tls;
#else
    if (!plain.connect(TG_HOST, TG_PORT)) return false;
    out = &plain;
#endif
    return true;
}

static bool sendMessage(const String &text) {
    WiFiClientSecure tls; WiFiClient plain; Client *c = nullptr;
    if (!openConn(tls, plain, c)) return false;
    String form = "chat_id=" + String(TG_CHAT_ID) + "&text=" + urlEncode(text);
    c->print(String("POST /bot") + TG_BOT_TOKEN + "/sendMessage HTTP/1.1\r\nHost: " + TG_HOST +
             "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " + form.length() +
             "\r\nConnection: close\r\n\r\n" + form);
    String body;
    bool ok = readReply(*c, body);
    c->stop();
    return ok;
}

static bool sendFile(const String &path, const String &name, const String &caption) {
    fs::FS *fs = gStore->fs();
    File f = fs->open(path, FILE_READ);
    if (!f) return false;
    size_t size = f.size();   // snapshot: the live session may still be growing

    WiFiClientSecure tls; WiFiClient plain; Client *c = nullptr;
    if (!openConn(tls, plain, c)) { f.close(); return false; }

    const String B = "----obdlogger7e3f9a";
    String head = "--" + B + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + TG_CHAT_ID +
                  "\r\n--" + B + "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption +
                  "\r\n--" + B + "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" + name +
                  "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    String tail = "\r\n--" + B + "--\r\n";
    c->print(String("POST /bot") + TG_BOT_TOKEN + "/sendDocument HTTP/1.1\r\nHost: " + TG_HOST +
             "\r\nContent-Type: multipart/form-data; boundary=" + B + "\r\nContent-Length: " +
             (head.length() + size + tail.length()) + "\r\nConnection: close\r\n\r\n");
    c->print(head);
    uint8_t buf[1024];
    size_t sent = 0;
    while (sent < size) {
        size_t n = f.read(buf, min(sizeof(buf), size - sent));
        if (n == 0) break;
        if (c->write(buf, n) != n) { f.close(); c->stop(); return false; }
        sent += n;
        vTaskDelay(1);
    }
    f.close();
    if (sent != size) { c->stop(); return false; }
    c->print(tail);
    String body;
    bool ok = readReply(*c, body);
    c->stop();
    return ok;
}

static int fileNumber(const String &n) {
    int us = n.indexOf('_'), dot = n.lastIndexOf('.');
    return (us > 0 && dot > us) ? n.substring(us + 1, dot).toInt() : -1;
}

static String fmtBytes(size_t n) { return n > 1048576 ? String(n / 1048576.0, 1) + " MB" : String(n / 1024) + " KB"; }

static uint32_t nvGet(const char *key) {
    Preferences p; p.begin("obd", true);
    uint32_t v = p.getUInt(key, 0);
    p.end();
    return v;
}
static void nvPut(const char *key, uint32_t v) {
    Preferences p; p.begin("obd", false);
    p.putUInt(key, v);
    p.end();
}

static bool runReport(bool force, uint32_t today) {
    uint32_t sentBoot = nvGet("tg_boot");

    struct F { String name; int n; size_t size; };
    std::vector<F> files;
    fs::FS *fs = gStore->fs();
    if (!fs) { gLastResult = "no storage"; return false; }
    File root = fs->open(gStore->dir().length() ? gStore->dir() : String("/"));
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        String nm = f.name();
        nm = nm.substring(nm.lastIndexOf('/') + 1);
        int n = fileNumber(nm);
        bool isCsv = nm.startsWith("session_"), isRaw = nm.startsWith("raw_");
        if (n < 0 || (!isCsv && !isRaw) || (uint32_t)n <= sentBoot && n != (int)gBoot) continue;
        if (isRaw && !TG_SEND_RAW) continue;
        if (f.size() < TG_MIN_BYTES && !force) continue;   // skip blank/no-adapter sessions (an on-demand send includes everything)
        files.push_back({nm, n, f.size()});
    }
    std::sort(files.begin(), files.end(), [](const F &a, const F &b) { return a.n != b.n ? a.n < b.n : a.name < b.name; });

    size_t total = 0;
    for (auto &x : files) total += x.size;
    struct tm tmv; time_t now = time(nullptr); localtime_r(&now, &tmv);
    char date[32]; strftime(date, sizeof(date), "%a %d %b %H:%M", &tmv);
    String msg = String("OBD logger report, ") + date + "\n" + files.size() + " file(s), " + fmtBytes(total) + " to send\n";
    auto v = [](const char *n) { return webUiValue(n); };
    if (!isnan(v("dpf_soot_level_g")))
        msg += "Latest: soot " + String(v("dpf_soot_level_g"), 1) + " g, " + String(v("dpf_dist_since_regen_mi"), 1) +
               " mi since regen, DPF " + String(v("dpf_diff_pressure_hpa"), 0) + " hPa, EGT " +
               String(v("egt_before_dpf_c"), 0) + " C, battery " + String(v("control_module_v"), 1) + " V\n";
    else
        msg += "No live engine reading right now (car off or adapter out of range).\n";
    if (strlen(KOFI_URL) > 0) msg += "\nFound this useful? " + String(KOFI_URL) + "\n";
    if (files.empty() && !force) { nvPut("tg_day", today); gLastResult = "nothing new to send"; return true; }
    if (!sendMessage(msg)) { gLastResult = "message failed"; return false; }

    uint32_t lastGoodBoot = sentBoot;
    for (auto &x : files) {
        Serial.printf("Report: sending %s (%u bytes)\n", x.name.c_str(), (unsigned)x.size);
        if (!sendFile(String(gStore->dir()) + "/" + x.name, x.name, x.name + " (" + fmtBytes(x.size) + ")")) {
            nvPut("tg_boot", lastGoodBoot);
            gLastResult = "failed on " + x.name;
            return false;
        }
        if ((uint32_t)x.n < gBoot && (uint32_t)x.n > lastGoodBoot) lastGoodBoot = x.n;
    }
    nvPut("tg_boot", lastGoodBoot);
    nvPut("tg_day", today);
    gLastResult = "sent " + String(files.size()) + " file(s) at " + date;
    return true;
}

static String liveStatusLine() {
    auto v = [](const char *n) { return webUiValue(n); };
    struct tm tmv; time_t now = time(nullptr); localtime_r(&now, &tmv);
    char t[16]; strftime(t, sizeof(t), "%H:%M", &tmv);
    if (isnan(v("dpf_soot_level_g")))
        return String("Status ") + t + ": car off or adapter out of range";
    return String("Status ") + t + ": rpm " + String(v("engine_rpm"), 0) + ", speed " + String(v("vehicle_speed"), 0) +
           " km/h\nsoot " + String(v("dpf_soot_level_g"), 1) + " g, " + String(v("dpf_dist_since_regen_mi"), 1) +
           " mi since regen\nDPF " + String(v("dpf_diff_pressure_hpa"), 0) + " hPa, EGT " + String(v("egt_before_dpf_c"), 0) +
           " C, battery " + String(v("control_module_v"), 1) + " V";
}

static const int EVQ = 4;
static char gEvQ[EVQ][224];
static int gEvCount = 0;
static portMUX_TYPE gEvMux = portMUX_INITIALIZER_UNLOCKED;

void reportEvent(const char *text) {
    if (strlen(TG_BOT_TOKEN) == 0) return;
    portENTER_CRITICAL(&gEvMux);
    if (gEvCount == EVQ) { memmove(gEvQ[0], gEvQ[1], sizeof(gEvQ[0]) * (EVQ - 1)); gEvCount--; }   // drop the oldest
    strncpy(gEvQ[gEvCount], text, sizeof(gEvQ[0]) - 1);
    gEvQ[gEvCount][sizeof(gEvQ[0]) - 1] = 0;
    gEvCount++;
    portEXIT_CRITICAL(&gEvMux);
}

static void drainEvents() {
    for (;;) {
        char msg[224];
        portENTER_CRITICAL(&gEvMux);
        if (gEvCount == 0) { portEXIT_CRITICAL(&gEvMux); return; }
        strncpy(msg, gEvQ[0], sizeof(msg));
        portEXIT_CRITICAL(&gEvMux);
        if (!sendMessage(String(msg))) return;          // stay queued, try again next pass
        portENTER_CRITICAL(&gEvMux);
        memmove(gEvQ[0], gEvQ[1], sizeof(gEvQ[0]) * (EVQ - 1));
        gEvCount--;
        portEXIT_CRITICAL(&gEvMux);
        Serial.println("Alert sent");
    }
}

// ---- incoming "/status" command: a grounded report, real numbers only, no invented thresholds ----
// Polled via getUpdates (long polling off — timeout=0 — so this never blocks the report task for long).
// Only messages from the configured TG_CHAT_ID are ever acted on. Parsing is deliberately crude string
// scanning (matching the rest of this file's style) rather than a JSON library dependency.
static long gUpdateOffset = 0;

static bool getUpdates(String &body) {
    WiFiClientSecure tls; WiFiClient plain; Client *c = nullptr;
    if (!openConn(tls, plain, c)) return false;
    String path = String("/bot") + TG_BOT_TOKEN + "/getUpdates?offset=" + String(gUpdateOffset) + "&timeout=0&limit=5";
    c->print(String("GET ") + path + " HTTP/1.1\r\nHost: " + TG_HOST + "\r\nConnection: close\r\n\r\n");
    bool ok = readReply(*c, body);
    c->stop();
    return ok;
}

// Extracts the string value of a "key":"value" pair after `from` in a crude, non-JSON-library way
// (values here never contain an escaped quote, which holds for chat ids, update ids and plain text
// status commands). Returns "" if not found.
static String extractAfter(const String &body, const String &key, int from) {
    int k = body.indexOf(key, from);
    if (k < 0) return "";
    int q1 = body.indexOf('"', k + key.length());
    if (q1 < 0) return "";
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return "";
    return body.substring(q1 + 1, q2);
}

// Builds the "/status" reply: every figure is either read live from the ECU right now, or is one of
// this project's own measured constants (see research_paper/reverse_engineering_dpf_monitoring.md
// section 3.3) — nothing here is an invented threshold or a generic industry table.
static String statusReplyText() {
    auto v = [](const char *n) { return webUiValue(n); };
    float soot = v("dpf_soot_level_g");
    struct tm tmv; time_t now = time(nullptr); localtime_r(&now, &tmv);
    char t[16]; strftime(t, sizeof(t), "%H:%M", &tmv);

    String out = String("Status ") + t + "\n";
    if (!isnan(soot)) {
        out += "[MEASURED] soot " + String(soot, 1) + " g, " + String(v("dpf_dist_since_regen_mi"), 1) +
               " mi since regen, DPF diff P " + String(v("dpf_diff_pressure_hpa"), 0) + " hPa\n";
        out += "[MEASURED] rpm " + String(v("engine_rpm"), 0) + ", speed " + String(v("vehicle_speed"), 0) +
               " km/h, EGT " + String(v("egt_before_dpf_c"), 0) + " C, battery " + String(v("control_module_v"), 1) + " V\n";
    } else {
        // Matches liveStatusLine()'s existing behaviour: no separate "read the last logged row from
        // SD" path yet (SessionStore has no such accessor) -- a real gap, not silently guessed around.
        out += "Car off or adapter out of range right now -- no live reading to report.\n";
    }

    if (!isnan(soot)) {
        // This project's own three measured regeneration events (17.6, 17.6, 18.0 g) — not a manufacturer
        // spec, not a generic table, our own car's real trigger range.
        const float TRIGGER_LOW = 17.6f, TRIGGER_HIGH = 18.0f;
        const float RATE_STEADY_LOW = 0.18f, RATE_STEADY_HIGH = 0.35f;   // measured: warm, steady driving
        const float RATE_SHORT_LOW = 0.6f, RATE_SHORT_HIGH = 1.1f;       // measured: short, cold trips
        if (soot >= TRIGGER_HIGH) {
            out += "[MEASURED] already at or above this car's regen trigger range -- expect one to start on the next warm drive.\n";
        } else {
            float steadyMi = (TRIGGER_LOW - soot) / RATE_STEADY_HIGH;
            float steadyMiMax = (TRIGGER_HIGH - soot) / RATE_STEADY_LOW;
            float shortMi = (TRIGGER_LOW - soot) / RATE_SHORT_HIGH;
            float shortMiMax = (TRIGGER_HIGH - soot) / RATE_SHORT_LOW;
            out += "[INFERRED, from this car's own measured rates] roughly " + String(steadyMi, 0) + "-" + String(steadyMiMax, 0) +
                   " mi of steady driving, or " + String(shortMi, 0) + "-" + String(shortMiMax, 0) +
                   " mi of short cold trips, before the next regen.\n";
        }
    }
    return out;
}

static void pollStatusCommand() {
    String body;
    if (!getUpdates(body)) return;
    int pos = 0;
    for (;;) {
        int idPos = body.indexOf("\"update_id\":", pos);
        if (idPos < 0) break;
        long id = body.substring(idPos + 12, body.indexOf(',', idPos)).toInt();
        if (id >= gUpdateOffset) gUpdateOffset = id + 1;   // ack every update seen, even ones we ignore below
        String chatId = extractAfter(body, "\"chat\":{\"id\":", idPos);
        String text = extractAfter(body, "\"text\":", idPos);
        pos = idPos + 12;
        if (chatId != String(TG_CHAT_ID)) continue;        // only the configured chat is ever acted on
        text.trim();
        text.toLowerCase();
        if (text == "/status" || text == "status") sendMessage(statusReplyText());
    }
}

static uint32_t gLastStatusMs = 0;
static volatile bool gForceStatus = false;

void reportStatusNow() { gForceStatus = true; }

static void reportTask(void *) {
    uint32_t pass = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (strlen(TG_BOT_TOKEN) == 0 || WiFi.status() != WL_CONNECTED || !timeValid()) continue;
        drainEvents();                                   // alerts first, every 5 s
        pollStatusCommand();                              // then check for an incoming /status request
#if TG_STATUS_ENABLED
        // Only reachable while the outer check above already found it online,
        // so this can't build a backlog while offline — it just doesn't run.
        // gForceStatus (the test button) bypasses the running/warm gate so a
        // test send works even with the engine off.
        {
            float rpm = webUiValue("engine_rpm"), coolant = webUiValue("coolant_temp");
            bool runningWarm = !isnan(rpm) && rpm > 0 && !isnan(coolant) && coolant >= TG_STATUS_MIN_COOLANT_C;
            bool due = gForceStatus || (runningWarm && millis() - gLastStatusMs >= TG_STATUS_INTERVAL_MIN * 60000UL);
            if (due) {
                gForceStatus = false;
                gLastStatusMs = millis();
                sendMessage(liveStatusLine());
            }
        }
#endif
        if (++pass % 4 != 0 && !gForce) continue;        // daily check every ~20 s
        if (millis() < gRetryAfter) continue;
        time_t now = time(nullptr);
        struct tm t; localtime_r(&now, &t);
        uint32_t today = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
        uint32_t lastDay = nvGet("tg_day");
        bool due = t.tm_hour * 60 + t.tm_min >= TG_SEND_HOUR * 60 + TG_SEND_MIN && lastDay != today;
        if (!due && !gForce) continue;
        bool force = gForce;
        gForce = false;
        Serial.println(force ? "Report: sending now (requested)" : "Report: daily send time reached");
        bool ok = runReport(force, today);
        Serial.printf("Report: %s\n", gLastResult.c_str());
        if (!ok) gRetryAfter = millis() + 10UL * 60UL * 1000UL;   // try again in 10 minutes
    }
}

void reportSendNow() { gForce = true; gRetryAfter = 0; }

String reportStatusJson() {
    Preferences prefs;
    prefs.begin("obd", true);
    String j = String("{\"enabled\":") + (strlen(TG_BOT_TOKEN) ? "true" : "false") + ",\"last_day\":" +
               prefs.getUInt("tg_day", 0) + ",\"sent_boot\":" + prefs.getUInt("tg_boot", 0) + ",\"result\":\"" + gLastResult + "\"}";
    prefs.end();
    return j;
}

void reportReset() {
    Preferences prefs;
    prefs.begin("obd", false);
    prefs.remove("tg_day");
    prefs.remove("tg_boot");
    prefs.end();
    gLastResult = "reset";
}

void reportBegin(SessionStore *store, uint32_t bootNumber) {
    gStore = store;
    gBoot = bootNumber;
    if (strlen(TG_BOT_TOKEN) == 0) { Serial.println("Telegram report: off (no bot token set)"); return; }
    Serial.printf("Telegram report: daily at %02d:%02d local time\n", TG_SEND_HOUR, TG_SEND_MIN);
    xTaskCreatePinnedToCore(reportTask, "report", 16384, nullptr, 1, nullptr, 0);
}

#endif  // ENABLE_TELEGRAM
