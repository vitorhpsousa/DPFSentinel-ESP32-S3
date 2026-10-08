#include "web_ui.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <math.h>
#include "../config.h"
#include "../obd/pid_registry.h"
#include "timekeeper.h"
#include "../report/telegram_report.h"
#include "ui_html.h"

static WebServer server(80);
static SessionStore *gStore = nullptr;
static uint32_t gBoot = 0;
static float gValues[COLUMN_COUNT];
static bool gLink = false;
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static int gRpmCol = -1;
static volatile bool gEngineOn = false;

// Home-network (station) state. The radio is only allowed to hunt for the home
// network at boot and while the engine is off, so it never disturbs the
// hotspot or the BLE link while driving.
enum StaState { STA_TRYING, STA_UP, STA_OFF };
static StaState gSta = STA_OFF;
static uint32_t gStaSince = 0;
static bool gMdnsUp = false;
static const uint32_t STA_TRY_MS = 20000, STA_RETRY_MS = 10UL * 60UL * 1000UL;

void webUiUpdate(const float *values, size_t count, bool linkUp) {
    portENTER_CRITICAL(&gMux);
    for (size_t i = 0; i < count && i < COLUMN_COUNT; i++) gValues[i] = values[i];
    gLink = linkUp;
    portEXIT_CRITICAL(&gMux);
    gEngineOn = gRpmCol >= 0 && !isnan(values[gRpmCol]) && values[gRpmCol] > 0;
}

static void handleLive() {
    float v[COLUMN_COUNT];
    bool link;
    portENTER_CRITICAL(&gMux);
    memcpy(v, gValues, sizeof(v));
    link = gLink;
    portEXIT_CRITICAL(&gMux);

    // Board clock in local time (empty until the clock has been set).
    char local[40] = "";
    if (timeValid()) {
        time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(local, sizeof(local), "%a %d %b %H:%M:%S", &tmv);
    }
    String j;
    j.reserve(1400);
    j += "{\"t\":" + String(millis()) + ",\"link\":" + (link ? "1" : "0") + ",\"session\":" + String(gBoot) +
         ",\"local\":\"" + local + "\",\"time\":\"" + timeSource() + "\",\"home\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) +
         "\",\"storage\":\"" + gStore->backend() + "\",\"free_mb\":" +
         String(gStore->healthy() ? (long)(gStore->freeBytes() / 1048576ULL) : -1L) + ",\"values\":{";
    for (size_t i = 0; i < COLUMN_COUNT; i++) {
        if (i) j += ',';
        j += '"';
        j += columnName(i);
        j += "\":";
        j += isnan(v[i]) ? String("null") : String(v[i], 2);
    }
    j += "}}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

static bool safeName(const String &n) {
    if (n.length() == 0 || n.length() > 40) return false;
    if (!(n.startsWith("session_") || n.startsWith("raw_"))) return false;
    for (size_t i = 0; i < n.length(); i++) {
        char c = n[i];
        if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) return false;
    }
    return n.indexOf("..") < 0;
}

float webUiValue(const char *name) {
    for (size_t i = 0; i < COLUMN_COUNT; i++)
        if (!strcmp(columnName(i), name)) {
            portENTER_CRITICAL(&gMux);
            float v = gValues[i];
            portEXIT_CRITICAL(&gMux);
            return v;
        }
    return NAN;
}

static void handleSend() {
    reportSendNow();
    server.send(200, "application/json", "{\"queued\":true}");
}

static void handleTestAlert() {
    reportEvent("TEST alert: the regen alert path works.\nA real one reads: DPF regeneration started, with soot, miles since the last regen, and exhaust temperature.");
    server.send(200, "application/json", "{\"queued\":true}");
}

static void handleStatusNow() {
    reportStatusNow();
    server.send(200, "application/json", "{\"queued\":true}");
}

static void handleReport() {
    if (server.hasArg("reset")) reportReset();
    server.send(200, "application/json", reportStatusJson());
}

static void handleTime() {
    bool set = false;
    String ms = server.arg("ms");
    if (ms.length()) set = timeSetFromPhone(strtoull(ms.c_str(), nullptr, 10));
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json",
                String("{\"set\":") + (set ? "true" : "false") + ",\"valid\":" + (timeValid() ? "true" : "false") +
                    ",\"source\":\"" + timeSource() + "\"}");
}

static void handleFiles() {
    String j = "[";
    fs::FS *fs = gStore->fs();
    if (fs) {
        File root = fs->open(gStore->dir().length() ? gStore->dir() : String("/"));
        bool first = true;
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            String nm = f.name();
            int slash = nm.lastIndexOf('/');
            if (slash >= 0) nm = nm.substring(slash + 1);
            if (f.isDirectory() || !safeName(nm)) continue;
            if (!first) j += ',';
            first = false;
            j += "{\"name\":\"" + nm + "\",\"size\":" + String((unsigned long)f.size()) + "}";
        }
    }
    j += "]";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

static void handleDownload() {
    String name = server.arg("f");
    if (!safeName(name)) { server.send(400, "text/plain", "bad file name"); return; }
    fs::FS *fs = gStore->fs();
    File f = fs ? fs->open(gStore->dir() + "/" + name, FILE_READ) : File();
    if (!f) { server.send(404, "text/plain", "not found or busy"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.streamFile(f, name.endsWith(".csv") ? "text/csv" : "text/plain");
    f.close();
}

// Two candidate networks (e.g. home Wi-Fi and a phone hotspot), tried
// alternately — each staStart() call picks the other one from last time, so a
// network that's out of range doesn't block trying the other.
static int gStaCandidate = 0;

static bool credsFor(int idx, const char **ssid, const char **pass) {
    if (idx == 0 && strlen(WIFI_STA_SSID) > 0) { *ssid = WIFI_STA_SSID; *pass = WIFI_STA_PASS; return true; }
    if (idx == 1 && strlen(WIFI_STA_SSID2) > 0) { *ssid = WIFI_STA_SSID2; *pass = WIFI_STA_PASS2; return true; }
    return false;
}

static void staStart() {
#if WIFI_DISABLED
    return;
#endif
    const char *ssid, *pass;
    int tried = 0;
    while (!credsFor(gStaCandidate, &ssid, &pass)) {
        gStaCandidate ^= 1;
        if (++tried > 2) return;   // neither slot configured
    }
    WiFi.mode(WIFI_AP_ENABLED ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(ssid, pass);
    gSta = STA_TRYING;
    gStaSince = millis();
}

static volatile bool gPaused = false;
void webUiSleep() {   // WiFi off for good (until restart); staService() stops retrying
    gPaused = true;
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

static void staService() {
    if (gPaused) return;
#if WIFI_DISABLED
    return;
#endif
    if (strlen(WIFI_STA_SSID) == 0 && strlen(WIFI_STA_SSID2) == 0) return;
    uint32_t now = millis();
    bool up = WiFi.status() == WL_CONNECTED;
    switch (gSta) {
        case STA_TRYING:
            if (up) {
                gSta = STA_UP;
                Serial.printf("Wi-Fi \"%s\" joined: http://%s  (or http://obd-logger.local)\n", WiFi.SSID().c_str(),
                              WiFi.localIP().toString().c_str());
#if TG_ALERT_WIFI
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Wi-Fi joined: %s\nIP %s\nhttp://%s", WiFi.SSID().c_str(),
                             WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
                    reportEvent(msg);
                }
#endif
#if NTP_ENABLED
                timeStartNtp();
#endif
                if (!gMdnsUp && MDNS.begin("obd-logger")) { MDNS.addService("http", "tcp", 80); gMdnsUp = true; }
            } else if (now - gStaSince > STA_TRY_MS) {
                Serial.println(WIFI_AP_ENABLED ? "Wi-Fi not in range — hotspot only" : "Wi-Fi not in range — will try the other network");
                WiFi.disconnect(false);
                if (WIFI_AP_ENABLED) WiFi.mode(WIFI_AP);
                gSta = STA_OFF;
                gStaSince = now;
                gStaCandidate ^= 1;   // next attempt tries the other network
            }
            break;
        case STA_UP:
            if (!up && now - gStaSince > 0) {
                // Lost the network (e.g. driven away): fall back to hotspot only.
                WiFi.disconnect(false);
                if (WIFI_AP_ENABLED) WiFi.mode(WIFI_AP);
                gSta = STA_OFF;
                gStaSince = now;
                Serial.println("Home Wi-Fi lost — will keep looking");
            }
            break;
        case STA_OFF:
            if ((!WIFI_AP_ENABLED || !gEngineOn) && now - gStaSince > (WIFI_AP_ENABLED ? STA_RETRY_MS : 30000UL)) staStart();
            break;
    }
}

static void webTask(void *) {
    for (;;) {
        server.handleClient();
        staService();
        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

void webUiBegin(SessionStore *store, uint32_t bootNumber) {
    gStore = store;
    gBoot = bootNumber;
    for (size_t i = 0; i < COLUMN_COUNT; i++) gValues[i] = NAN;

    for (size_t i = 0; i < COLUMN_COUNT; i++) if (!strcmp(columnName(i), "engine_rpm")) gRpmCol = (int)i;
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname("obd-logger");
#if WIFI_AP_ENABLED
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, 6, 0, 4);
    Serial.printf("Web UI: join Wi-Fi \"%s\" (password %s), then open http://%s\n", WIFI_AP_SSID, WIFI_AP_PASS,
                  WiFi.softAPIP().toString().c_str());
#else
    WiFi.mode(WIFI_STA);
    Serial.println("Web UI: home Wi-Fi only (hotspot disabled)");
#endif

    server.on("/", []() { server.send_P(200, "text/html", UI_HTML); });
    server.on("/api/live", handleLive);
    server.on("/api/files", handleFiles);
    server.on("/api/time", handleTime);
    server.on("/api/send", handleSend);
    server.on("/api/report", handleReport);
    server.on("/api/testalert", handleTestAlert);
    server.on("/api/statusnow", handleStatusNow);
    server.on("/dl", handleDownload);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    server.begin();
    staStart();
    xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
}
