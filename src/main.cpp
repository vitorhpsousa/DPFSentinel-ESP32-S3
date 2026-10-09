#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "config.h"
#include "dpf/dpf_monitor.h"
#include "dpf/health.h"
#include "logging/session_store.h"
#include "obd/elm_client.h"
#include "obd/isotp.h"
#include "obd/pid_registry.h"
#include "report/regen_watch.h"
#include "report/telegram_report.h"
#include "web/timekeeper.h"
#include "web/web_ui.h"
#include <WiFi.h>
#if AUDIO_ENABLED
#include "audio/alert_audio.h"
#else
enum AlertKind { REGEN_START, REGEN_END, SOOT_AMBER, SOOT_RED, READY, TEST };
static inline void audioAlert(AlertKind) {}
static inline bool audioBegin() { return true; }
static inline void audioToggleMute() {}
static inline bool audioIsMuted() { return false; }
static inline void audioJingle(int) {}
#endif
#if HAS_DISPLAY
#include "board/board.h"
#include "ui/soot_ui.h"
#include "ui/pages.h"
#include "input/touch.h"
#endif

static ElmClient elm;
static RegenWatch regenWatch;
static SessionStore store;
#if HAS_DISPLAY
static PagesData pd;
static void uiTask(void *);
static TaskHandle_t uiTaskHandle = nullptr;
#endif

#if BLE_SCAN_ONLY

void setup() { Serial.begin(115200); delay(1000); Serial.println("*** BLE_SCAN_ONLY ***"); }
void loop() { ElmClient::scanAndPrint(8000); delay(1500); }

#elif BLE_DUMP_SERVICES

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("*** BLE_DUMP_SERVICES ***");
    if (!elm.begin()) { Serial.println("Connect failed."); return; }
    elm.dumpServices();
}
void loop() { delay(5000); }

#else

// One entry per unique (header, request): several PID_TABLE rows share a
// request (everything in the 2103 reply), so it goes out once and every
// row decodes from the same payload.
struct Req {
    const char *header, *request, *frames, *rxId;
    uint16_t periodMs;
    uint32_t nextDue = 0, lastGood = 0;
    uint8_t fails = 0;
    bool noCount = false;   // adapter rejected the frame-count digit
    uint8_t payload[96];
    size_t len = 0;
};
static Req reqs[PID_TABLE_LEN];
static size_t reqCount = 0;
static int8_t reqOf[PID_TABLE_LEN];

// +1: a spare slot a missing column name resolves to (see colOf) instead of
// index -1, which would be undefined behaviour. Always NaN — assembleRow()
// only ever writes indices < COLUMN_COUNT.
static float values[COLUMN_COUNT + 1];
static int colRpm, colSpeed, colLoad, colDp, colSoot, colEgt, colRegen, colCat, colOdo, colDist, colOdoRegen;
static uint32_t bootNumber, lastRow = 0, blankRows = 0;

static int colOf(const char *name) {
    for (size_t i = 0; i < COLUMN_COUNT; i++) if (strcmp(columnName(i), name) == 0) return (int)i;
    return (int)COLUMN_COUNT;   // the spare NaN slot — a vehicle profile without this column reads NaN, not garbage
}

static void buildRequestTable() {
    for (size_t i = 0; i < PID_TABLE_LEN; i++) {
        const PidDef &p = PID_TABLE[i];
        int found = -1;
        for (size_t r = 0; r < reqCount; r++)
            if (!strcmp(reqs[r].header, p.header) && !strcmp(reqs[r].request, p.requestHex)) { found = (int)r; break; }
        if (found < 0) {
            found = (int)reqCount++;
            reqs[found].header = p.header;
            reqs[found].request = p.requestHex;
            reqs[found].frames = p.frames;
            reqs[found].rxId = rxIdFor(p.header);
            reqs[found].periodMs = p.periodMs;
        } else if (p.periodMs < reqs[found].periodMs) {
            reqs[found].periodMs = p.periodMs;
        }
        reqOf[i] = (int8_t)found;
    }
}

static void runRequest(Req &r) {
    uint32_t now = millis();
    bool ok = false;
    if (elm.setHeader(r.header)) {
        const char *frames = r.noCount ? "" : r.frames;
        String raw = elm.queryRaw(r.request, frames);
        // Some clones answer "?" to the ELM327 frame-count digit; retry
        // without it and remember, so this request works on any adapter.
        if (!r.noCount && r.frames[0] && raw.indexOf('?') >= 0 && raw.indexOf('|') < 0) {
            r.noCount = true;
            Serial.printf("Adapter rejected count digit on %s — sending it plain from now on\n", r.request);
            frames = "";
            raw = elm.queryRaw(r.request, frames);
        }
        // While nothing decodes (ECU asleep, adapter still connected) keep only the
        // first few cycles and then one in sixty, so a parked car doesn't fill the
        // storage with "NO DATA".
        static uint32_t deadRawSkip = 0;
        if (blankRows < 5 || (++deadRawSkip % 60) == 0)
            store.queueRaw(now, r.header, (String(r.request) + frames).c_str(), raw);
#if SERIAL_RAW_ECHO
        Serial.printf("RAW %s %s%s -> %s\n", r.header, r.request, frames, raw.c_str());
#endif
        size_t n = isotpExtract(raw.c_str(), r.rxId, r.payload, sizeof(r.payload));
        // The payload must begin with the expected service/PID bytes.
        const PidDef *first = nullptr;
        for (size_t i = 0; i < PID_TABLE_LEN; i++) if (&reqs[reqOf[i]] == &r) { first = &PID_TABLE[i]; break; }
        if (n > 0 && first && n >= first->prefixLen && !memcmp(r.payload, first->prefix, first->prefixLen)) {
            r.len = n;
            r.lastGood = now;
            r.fails = 0;
            ok = true;
        }
    }
    if (!ok) { if (r.fails < 255) r.fails++; }
    // Requests the ECU never answers (the standard probes) back off to one
    // try every 30 s instead of stalling the fast loop on ATST96 timeouts.
    uint32_t period = (r.fails >= 5) ? 30000 : r.periodMs;
    r.nextDue = now + period;
}

static void assembleRow() {
    uint32_t now = millis();
    for (size_t i = 0; i < PID_TABLE_LEN; i++) {
        const Req &r = reqs[reqOf[i]];
        uint32_t stale = r.periodMs * 3 > 3000 ? r.periodMs * 3 : 3000;
        values[i] = (r.lastGood && now - r.lastGood <= stale) ? PID_TABLE[i].decode(r.payload, r.len) : NAN;
    }
    float odo = values[colOdo], dist = values[colDist];
    values[colOdoRegen] = (!isnan(odo) && !isnan(dist)) ? odo - dist : NAN;
}

static void printRow() {
    Serial.printf("t=%lus rpm=%.0f spd=%.0f load=%.0f%% dpfP=%.1fhPa soot=%.1fg egt=%.0fC cat=%.0fC regen=%.0f since=%.1fmi | %s wf=%lu rm=%lu wifi=%ddBm\n",
                  (unsigned long)(millis() / 1000), values[colRpm], values[colSpeed], values[colLoad], values[colDp],
                  values[colSoot], values[colEgt], values[colCat], values[colRegen], values[colDist],
                  store.backend(), (unsigned long)store.writeFailures(), (unsigned long)store.remounts(),
                  WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0);
}

static bool connectAdapter() {
    elm.close();
    if (!elm.begin()) return false;
    if (!elm.initAdapter()) { Serial.println("Adapter init sequence failed"); return false; }
    Serial.println("ELM327 adapter ready");
    for (size_t r = 0; r < reqCount; r++) { reqs[r].nextDue = 0; reqs[r].fails = 0; }
    return true;
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    // Native USB needs ~1 s to reconnect after a reset: wait (max 3 s) so the first lines are not lost.
    for (uint32_t t0 = millis(); !Serial && millis() - t0 < 3000;) delay(10);
    Serial.printf("Boot: reset reason %d (1=power-on 3=sw 4=panic 5/6/7=watchdog 9=brownout)\n", (int)esp_reset_reason());   // never stall on a full USB-CDC buffer when no computer is attached (power bank / car)
    delay(1000);
    Serial.printf("DPF Sentinel | board=%s | storage=%s | wifi=%s | telegram=%s\n",
#if defined(BOARD_CYD35)
                  "cyd35",
#else
                  "freenove",
#endif
                  USE_SD ? "SD card" : "internal flash (30 min max)",
                  strlen(WIFI_STA_SSID) ? WIFI_STA_SSID : "none configured", ENABLE_TELEGRAM ? "on" : "off");
    timeInitTz();
#if HAS_DISPLAY
    if (boardInit()) { uiBegin(); pagesBegin(); } else Serial.println("Display init failed — continuing without a screen");
    for (auto &h : pd.sootHistory) h = NAN;
    pd.tripSootStart = pd.tripSootPeak = NAN;
    if (!touchBegin()) Serial.println("Touch init failed");
    if (!audioBegin()) Serial.println("Audio init failed (no beeps)");
    else if (BOOT_JINGLE >= 1 && BOOT_JINGLE <= 2) audioJingle(BOOT_JINGLE);
    else audioAlert(READY);   // Wire is already up from touchBegin()
#endif

    timeRtcBegin();   // after boardInit(): on the CYD the RTC shares the touch I2C bus

    Preferences prefs;
    prefs.begin("obd", false);
    bootNumber = prefs.getUInt("boot", 0) + 1;
    prefs.putUInt("boot", bootNumber);
    prefs.end();
    healthBegin();

    for (size_t i = 0; i <= COLUMN_COUNT; i++) values[i] = NAN;
    colRpm = colOf("engine_rpm"); colSpeed = colOf("vehicle_speed"); colLoad = colOf("engine_load_pct");
    colDp = colOf("dpf_diff_pressure_hpa"); colSoot = colOf("dpf_soot_level_g"); colEgt = colOf("egt_before_dpf_c");
    colRegen = colOf("dpf_regen_active"); colCat = colOf("dpf_zone_temp_c"); colOdo = colOf("odometer_mi");
    colDist = colOf("dpf_dist_since_regen_mi"); colOdoRegen = colOf("dpf_odo_at_last_regen_mi");
    buildRequestTable();

    Serial.println("[boot] mounting storage (SD card)...");
    if (store.begin(bootNumber)) Serial.printf("Logging to %s as session_%lu.csv (+ raw_%lu.log)\n", store.backend(),
                                               (unsigned long)bootNumber, (unsigned long)bootNumber);
    else Serial.println("Storage init failed — logging to Serial only");

#if LED_REGEN_PIN >= 0
    pinMode(LED_REGEN_PIN, OUTPUT);
    pinMode(LED_TEMP_WARN_PIN, OUTPUT);
    digitalWrite(LED_REGEN_PIN, LOW);
    digitalWrite(LED_TEMP_WARN_PIN, LOW);
#endif

#if WEB_ENABLED
    Serial.println("[boot] starting WiFi + web page...");
    webUiBegin(&store, bootNumber);
    reportBegin(&store, bootNumber);
#endif

#if HAS_DISPLAY
    xTaskCreatePinnedToCore(uiTask, "ui", 12288, nullptr, 1, &uiTaskHandle, 0);
#endif
    Serial.println("[boot] setup finished");
    Serial.println("Connecting to BLE ELM327 adapter...");
    connectAdapter();
}

#if HAS_DISPLAY
static bool everHadData = false;
static float tripOdo0 = NAN, tripSoot0 = NAN;
static bool prevRegen = false;
static uint32_t regenStartMs = 0;
static float regenPeak = 0;
static uint32_t lastHist = 0;
static bool dimmed = false;
static volatile bool redrawNow = false;   // set by touch so a page change shows immediately, not at the next 1 s tick

static void setBacklight(uint8_t v) { boardSetBacklight(v); }

static void trackTrip() {
    float odo = values[colOdo], soot = values[colSoot], spd = values[colSpeed];
    if (!isnan(odo)) { if (isnan(tripOdo0)) tripOdo0 = odo; pd.tripMiles = odo - tripOdo0; }
    if (!isnan(soot)) {
        if (isnan(tripSoot0)) { tripSoot0 = soot; pd.tripSootPeak = soot; }
        pd.tripSootStart = tripSoot0;
        if (soot > pd.tripSootPeak) pd.tripSootPeak = soot;
    }
    if (!isnan(spd) && spd > pd.maxSpeed) pd.maxSpeed = spd;
    bool regen = !isnan(values[colRegen]) && values[colRegen] != 0.0f;
    float egt = !isnan(values[colEgt]) ? values[colEgt] : values[colCat];
    if (regen && !prevRegen) { regenStartMs = millis(); regenPeak = 0; pd.regenCount++; }
    if (regen && !isnan(egt) && egt > regenPeak) regenPeak = egt;
    if (!regen && prevRegen) {
        pd.lastRegenEndMs = millis();
        pd.lastRegenPeakEgt = regenPeak;
        pd.lastRegenMinutes = (millis() - regenStartMs) / 60000UL;
    }
    prevRegen = regen;
    if (millis() - lastHist >= 60000UL) {   // one soot sample a minute -> 4 h of history
        lastHist = millis();
        memmove(pd.sootHistory, pd.sootHistory + 1, sizeof(float) * 239);
        pd.sootHistory[239] = soot;
    }
}

// ---- Demo mode: cycles fake states like the Pi demo, with the matching sounds and the Rondo at the start.
static bool demoActive = false;
static uint32_t demoStart = 0;
static int demoLastIdx = -1;
static const uint32_t DEMO_STATE_MS = 8000;
static void demoToggle(int startIdx = 0) {
    demoActive = !demoActive;
    demoStart = millis() - (uint32_t)startIdx * DEMO_STATE_MS; demoLastIdx = -1;
    if (demoActive) { pagesGoto(PAGE_SOOT); audioJingle(1); }
    Serial.printf("demo %s\n", demoActive ? "ON" : "OFF");
}
// Returns the demo UiState for the current step and plays that step's sound when it changes.
static void demoFill(UiState &st) {
    static const float soot[7]  = {NAN, NAN, 8.2f, 15.5f, 21.0f, 28.0f, 30.2f};
    static const float egt[7]   = {NAN, NAN, 245, 310, 480, 520, 600};
    int idx = ((millis() - demoStart) / DEMO_STATE_MS) % 7;
    if (idx != demoLastIdx) {
        if (idx == 3) audioAlert(SOOT_AMBER);
        else if (idx == 4) audioAlert(SOOT_RED);
        else if (idx == 6) audioAlert(REGEN_START);
        else if (idx == 0 && demoLastIdx == 6) audioAlert(REGEN_END);
        demoLastIdx = idx;
    }
    st.hasData = idx >= 1;
    st.stale = idx <= 1;
    if (idx >= 2) {
        st.soot = soot[idx]; st.catTemp = egt[idx]; st.rpm = idx == 6 ? 1900 : 1450; st.speed = idx == 6 ? 0 : 42;
        st.coolant = 88; st.diffP = 8.0f + idx * 3; st.intercooler = 61; st.maf = 24.3f;
        st.sinceRegen = idx >= 4 ? 180.0f + idx * 40 : 87.4f; st.odometer = 125760;
        st.regen = (idx == 6);
    }
}

static void updateScreen() {
    UiState st;
    st.soot = values[colSoot]; st.rpm = values[colRpm]; st.speed = values[colSpeed];
    st.coolant = values[colOf("coolant_temp")]; st.diffP = values[colDp]; st.catTemp = values[colCat];
    st.intercooler = values[colOf("intercooler_temp_c")]; st.maf = values[colOf("maf_flow")];
    st.sinceRegen = values[colDist]; st.odometer = values[colOdo];
    st.regen = !isnan(values[colRegen]) && values[colRegen] != 0.0f;
    bool any = false;
    for (size_t i = 0; i < PID_TABLE_LEN; i++) if (!isnan(values[i])) { any = true; break; }
    if (any) everHadData = true;
    st.hasData = everHadData;
    st.stale = !elm.connected() || !any;
    snprintf(st.ip, sizeof st.ip, "%s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "no WiFi");
    snprintf(st.ssid, sizeof st.ssid, "%s", WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "");
    {   // minute resolution: the text only changes once a minute, so the frame hash only changes then
        double tn = unixNow();
        if (tn > 1.7e9) { time_t tt = (time_t)tn; struct tm tmv; localtime_r(&tt, &tmv); strftime(st.clock, sizeof st.clock, "%a %d %b %H:%M", &tmv); }
        else snprintf(st.clock, sizeof st.clock, "clock not set");
    }

    if (demoActive) demoFill(st);
    else trackTrip();
    pd.sootHistoryStepMin = 1;
    pd.bleLinked = elm.connected();
    snprintf(pd.sdBackend, sizeof pd.sdBackend, "%s", store.backend());
    // usedBytes() walks the FAT (slow on a 30 GB card). Plenty of room (>1000 MB free at the last check):
    // look again once a day; when it's getting low, every 5 minutes.
    static uint32_t lastFree = 0;
    uint32_t freeEvery = pd.sdFreeMB > 1000 ? 86400000UL : 300000UL;
    if (SD_FREE_CHECK && (lastFree == 0 || millis() - lastFree > freeEvery)) { lastFree = millis() | 1; pd.sdFreeMB = (uint32_t)(store.freeBytes() / (1024 * 1024)); }
    pd.bootNumber = bootNumber;
    double t = unixNow();
    pd.clockSynced = t > 1.7e9;
    if (pd.clockSynced) { time_t tt = (time_t)t; struct tm tmv; localtime_r(&tt, &tmv); strftime(pd.clockText, sizeof pd.clockText, "%d %b %H:%M", &tmv); }
    else snprintf(pd.clockText, sizeof pd.clockText, "not synced");
    pd.uptimeS = millis() / 1000;
    pd.freeHeapKB = ESP.getFreeHeap() / 1024; pd.freePsramKB = ESP.getFreePsram() / 1024;
    {
        const HealthState &h = healthGet();
        pd.batteryV = h.lastBatteryV;
        pd.healthRegensToday = (int16_t)healthRegensToday();
        pd.healthRegensThisWeek = (int16_t)healthRegensThisWeek();
        pd.healthAvgRegenIntervalMi = healthAvgRegenIntervalMi();
        pd.healthRegensSinceOil = h.regensSinceOil;
        pd.healthOilChangeOdometerMi = h.oilChangeOdometerMi;
        pd.healthLastWarmupMin = h.lastWarmupMinutes;
        pd.healthWarmupMedianMin = healthWarmupMedianMin();
    }
    pagesRender(st, pd);
}

// Touch: tap left/right third changes page; any touch wakes a dimmed screen (and is otherwise ignored). Never dims during a regen or at red soot.
static void serviceTouchAndBacklight() {
    TouchEvent ev = touchPoll();
    bool keepBright = (!isnan(values[colRegen]) && values[colRegen] != 0.0f) || (!isnan(values[colSoot]) && values[colSoot] >= 17.0f);
    if (ev.type != TouchEvent::NONE) {
        redrawNow = true;
        if (dimmed) { dimmed = false; setBacklight(255); }
        else if (ev.type == TouchEvent::LONG) { audioToggleMute(); pagesSetMuted(audioIsMuted()); }   // hold 0.7 s anywhere
        // Tap navigation: left third = previous page, right third = next page, middle third does nothing.
        else if (ev.type == TouchEvent::TAP && ev.x < UI_W / 3) pagesPrev();
        else if (ev.type == TouchEvent::TAP && ev.x >= 2 * UI_W / 3) pagesNext();
    }
    bool idle = millis() - touchLastActivityMs() > 60000UL;
    if (idle && !keepBright && !dimmed) { dimmed = true; setBacklight(40); }
    if ((!idle || keepBright) && dimmed) { dimmed = false; setBacklight(255); }
}
#endif

#if HAS_DISPLAY
// Touch and screen run in their own task: the BLE adapter scan in loop() blocks for
// seconds at a time, which froze the touch screen.
static void uiTask(void *) {
    uint32_t lastDraw = 0;
    for (;;) {
        serviceTouchAndBacklight();
        if (redrawNow || millis() - lastDraw >= 1000) { redrawNow = false; lastDraw = millis(); updateScreen(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

static void serialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'd') { Serial.println("--- dumping logs ---"); store.dumpAll(Serial); Serial.println("--- dump complete ---"); }
        else if (c == 'p') { store.tailSession(Serial, bootNumber - 1, 240); }
#if HAS_DISPLAY
        else if (c == 'D') demoToggle();
        else if (c == 'G') demoToggle(6);   // demo starting on the regen screen (for review)
#endif
        else if (c == 'r') { Serial.println("alert red"); audioAlert(SOOT_RED); }
        else if (c == 'l') { Serial.println("alert regen end"); audioAlert(REGEN_END); }
        else if (c == 'a') { Serial.println("alert amber"); audioAlert(SOOT_AMBER); }
        else if (c == 's') { Serial.println("alert regen start"); audioAlert(REGEN_START); }
        else if (c >= '1' && c <= '2') { Serial.printf("jingle %c\n", c); audioJingle(c - '0'); }
        else if (c == 'e') { store.eraseAll(); Serial.println("--- logs erased, reboot to start a new session ---"); }
        // 'O' = oil change reference reset. There's no touchscreen keyboard on this board
        // to enter a value, so instead of a number this just means "reset the counter now,
        // I just changed the oil": it records the CURRENT odometer reading and zeroes the
        // regens-since-oil-change counter, persisted to flash immediately. See dpf/health.h.
        else if (c == 'O') {
            if (isnan(values[colOdo])) {
                Serial.println("Oil-change reset ignored: no live odometer reading yet (car not connected). Try again once it's logging.");
            } else {
                healthOilChangeNow(values[colOdo]);
                Serial.printf("Oil-change reference reset at odometer %.1f mi; regens-since-oil counter cleared\n", values[colOdo]);
            }
        }
    }
}

void loop() {
    static uint32_t lastReconnect = 0;
    serialCommands();
    uint32_t now = millis();

    // Housekeeping that must not depend on the adapter link: clock marker, raw
    // log flush, and the web page's view of the current state.
    static uint32_t lastHousekeeping = 0;
    if (now - lastHousekeeping >= 1000) {
        lastHousekeeping = now;
        timePoll(store);
        { static uint32_t lastRssi = 0; if (now - lastRssi >= 5000) { lastRssi = now; if (WiFi.status() == WL_CONNECTED) Serial.printf("WIFI %s rssi=%d dBm\n", WiFi.SSID().c_str(), (int)WiFi.RSSI()); else Serial.println("WIFI not connected"); } }
        store.flushRaw();
#if WEB_ENABLED
        webUiUpdate(values, COLUMN_COUNT, elm.connected());
#endif

    }

    if (!elm.connected()) {
        if (now - lastReconnect >= 5000) {
            lastReconnect = now;
            Serial.println("Adapter link down — reconnecting...");
            for (size_t i = 0; i <= COLUMN_COUNT; i++) values[i] = NAN;
            store.writeRow(now, unixNow(), values, COLUMN_COUNT);
            connectAdapter();
        }
        delay(10);
        return;
    }

    // Serve the most overdue request; the loop never blocks longer than one
    // adapter round trip, so fast PIDs (RPM, load) keep their cadence while
    // slow ones (coolant, EGR, EPS) simply come round less often.
    Req *best = nullptr;
    for (size_t r = 0; r < reqCount; r++) {
        if (now >= reqs[r].nextDue && (!best || reqs[r].nextDue < best->nextDue)) best = &reqs[r];
    }
    if (best) runRequest(*best);

    now = millis();
    if (now - lastRow >= ROW_INTERVAL_MS) {
        lastRow = now;
        assembleRow();
        store.writeRow(now, unixNow(), values, COLUMN_COUNT);
        printRow();
#if TG_ALERT_REGEN
        {
            RegenEvent ev = regenWatch.update(now, unixNow(), values[colRegen], values[colEgt], values[colSoot], values[colDist], values[colDp]);
            if (ev.type != RegenEvent::NONE) {
                Serial.printf("REGEN EVENT: %s\n", ev.text);
                if (ev.type == RegenEvent::START) audioAlert(REGEN_START);
                else if (ev.type == RegenEvent::END) audioAlert(REGEN_END);
                reportEvent(ev.text);
            }
            healthUpdate(values, COLUMN_COUNT, ev);
            if (healthFastRegenAlert())
                reportEvent("Health: 3 regens in a row under 100 mi apart -- oil dilution or a stuck EGR/DPF sensor is worth checking");
            if (healthOilReminderAlert())
                reportEvent("Health: 15 regens since the last oil-change reference -- check the dipstick (send 'O' once it's done)");
            if (healthSlowWarmupAlert())
                reportEvent("Health: warm-up to 80C took well over the usual time -- possible stuck-open thermostat");
        }
#endif

        {   // soot threshold beeps: amber >= 14 g, red >= 17 g; 0.5 g hysteresis so it doesn't chatter
            static int zone = 0;
            float sv = values[colSoot];
            if (!isnan(sv)) {
                int z = zone;
                if (sv >= 17.0f) z = 2; else if (sv >= 14.0f && z < 1) z = 1;
                if (z == 2 && sv < 16.5f) z = sv >= 14.0f ? 1 : 0;
                if (z == 1 && sv < 13.5f) z = 0;
                if (z > zone) audioAlert(z == 2 ? SOOT_RED : SOOT_AMBER);
                zone = z;
            }
        }
        bool any = false;
        for (size_t i = 0; i < PID_TABLE_LEN; i++) if (!isnan(values[i])) { any = true; break; }
        blankRows = any ? 0 : blankRows + 1;
        if (blankRows == BLANK_ROWS_REINIT) {
            Serial.println("No decodable data — re-initialising adapter");
            elm.initAdapter();
            for (size_t r = 0; r < reqCount; r++) { reqs[r].nextDue = 0; reqs[r].fails = 0; }
        } else if (blankRows >= BLANK_ROWS_RECONNECT) {
            blankRows = 0;
            Serial.println("Still no data — reconnecting BLE");
            elm.close();
        }

        if (isDpfIdleBlockageFlagged(values[colDp], values[colRpm]))
            Serial.println("*** DPF idle pressure elevated — possible blockage ***");
#if LED_REGEN_PIN >= 0
        digitalWrite(LED_REGEN_PIN, (!isnan(values[colRegen]) && values[colRegen] != 0.0f) ? HIGH : LOW);
        float hot = !isnan(values[colEgt]) ? values[colEgt] : values[colCat];
        digitalWrite(LED_TEMP_WARN_PIN, (!isnan(hot) && hot >= DPF_TEMP_WARN_THRESHOLD_C) ? HIGH : LOW);
#endif
    }
    delay(1);
}

#endif
