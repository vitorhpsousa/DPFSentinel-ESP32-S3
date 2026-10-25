#include "health.h"
#include <Preferences.h>
#include <string.h>
#include "../obd/pid_registry.h"
#include "../web/timekeeper.h"

// Guessed thresholds — see the block comment in health.h for sources.
static const float FAST_REGEN_THRESHOLD_MI = 100.0f;     // metric #2: "3 consecutive regens closer than this"
static const uint16_t OIL_REMINDER_REGENS = 15;           // metric #13
static const float SLOW_WARMUP_RATIO = 1.3f;              // metric #7: 30% over the rolling median
static const uint8_t SLOW_WARMUP_MIN_SAMPLES = 3;         // don't judge off one or two warm-ups
static const float WARMUP_COLD_C = 40.0f, WARMUP_DONE_C = 80.0f;

// Next-regen estimate. Regens on this car have started at 17.65-19.61 g; the
// lowest is the default trigger so the estimate errs towards "sooner". The
// learnt trigger is the median of recent first-in-cycle starts, clamped, so one
// odd start (a retry after an abort, a forced regen) can't drag it around.
static const float DEFAULT_TRIGGER_G = 17.65f;
static const float TRIGGER_MIN_G = 16.5f, TRIGGER_MAX_G = 20.0f;
static const float RESET_MAX_DIST_MI = 1.0f;     // a real reset lands near 0 mi...
static const float RESET_MAX_SOOT_G = 6.0f;      // ...with the filter nearly empty
static const uint8_t RESET_CONFIRM_ROWS = 2;     // one glitchy row is not a reset
static const float MIN_LIVE_SPAN_MI = 20.0f;     // live g/mi is too noisy over fewer miles
static const float MIN_LIVE_RATE = 0.02f;        // g/mi; below this the ratio is meaningless
static const float MAX_ESTIMATE_MI = 999.0f;

// Bumped whenever HealthState's on-disk layout changes, so a stale blob from
// an older build is discarded instead of read as garbage.
static const uint8_t NVS_LAYOUT_VERSION = 1;

static HealthState g_h;
static Preferences g_prefs;
static bool g_dirty = false;   // set true by anything that must survive a reboot

// ---- column lookup (same pattern as main.cpp's colOf) ----
static int colOf(const char *name) {
    for (size_t i = 0; i < COLUMN_COUNT; i++)
        if (strcmp(columnName(i), name) == 0) return (int)i;
    return -1;
}
static int s_colOdo = -2, s_colDist = -2, s_colCoolant = -2, s_colBatt = -2, s_colSoot = -2;
static void resolveColumns() {
    if (s_colOdo != -2) return;
    s_colOdo = colOf("odometer_mi");
    s_colDist = colOf("dpf_dist_since_regen_mi");
    s_colCoolant = colOf("coolant_temp");
    s_colBatt = colOf("control_module_v");
    s_colSoot = colOf("dpf_soot_level_g");
}
static float valAt(const float *values, size_t count, int col) {
    return (col >= 0 && (size_t)col < count) ? values[col] : NAN;
}

// ---- persistence ----
void healthBegin() {
    g_prefs.begin("health", false);
    if (g_prefs.getUChar("ver", 0) == NVS_LAYOUT_VERSION) {
        size_t rn = g_prefs.isKey("regHist") ? g_prefs.getBytesLength("regHist") : 0;
        if (rn == sizeof(g_h.regenHistory)) g_prefs.getBytes("regHist", g_h.regenHistory, rn);
        size_t wn = g_prefs.isKey("warmHist") ? g_prefs.getBytesLength("warmHist") : 0;
        if (wn == sizeof(g_h.warmupMinutes)) g_prefs.getBytes("warmHist", g_h.warmupMinutes, wn);
        g_h.regenCount = g_prefs.getUChar("regCount", 0);
        g_h.regenHead = g_prefs.getUChar("regHead", 0);
        g_h.warmupCount = g_prefs.getUChar("warmCount", 0);
        g_h.warmupHead = g_prefs.getUChar("warmHead", 0);
        g_h.regensSinceOil = g_prefs.getUShort("sinceOil", 0);
        g_h.oilChangeOdometerMi = g_prefs.isKey("oilOdo") ? g_prefs.getFloat("oilOdo", 0.0f) : 0.0f;
        g_h.lastWarmupMinutes = g_prefs.isKey("lastWarm") ? g_prefs.getFloat("lastWarm", NAN) : NAN;
        g_h.baseSootG = g_prefs.getFloat("baseSoot", 0.0f);
        g_h.baseDistMi = g_prefs.getFloat("baseDist", 0.0f);
        size_t tn = g_prefs.isKey("trigHist") ? g_prefs.getBytesLength("trigHist") : 0;
        if (tn == sizeof(g_h.triggerHist)) g_prefs.getBytes("trigHist", g_h.triggerHist, tn);
        g_h.triggerCount = g_prefs.getUChar("trigCount", 0);
        g_h.triggerHead = g_prefs.getUChar("trigHead", 0);
        g_h.cycleHadStart = g_prefs.getBool("cycStart", false);
        g_h.lastRateGPerMi = g_prefs.isKey("lastRate") ? g_prefs.getFloat("lastRate", NAN) : NAN;
    } else {
        // First boot with this layout (or an older one): start clean and stamp it.
        g_prefs.putUChar("ver", NVS_LAYOUT_VERSION);
    }
}

static void healthSave() {
    g_prefs.putBytes("regHist", g_h.regenHistory, sizeof(g_h.regenHistory));
    g_prefs.putBytes("warmHist", g_h.warmupMinutes, sizeof(g_h.warmupMinutes));
    g_prefs.putUChar("regCount", g_h.regenCount);
    g_prefs.putUChar("regHead", g_h.regenHead);
    g_prefs.putUChar("warmCount", g_h.warmupCount);
    g_prefs.putUChar("warmHead", g_h.warmupHead);
    g_prefs.putUShort("sinceOil", g_h.regensSinceOil);
    g_prefs.putFloat("oilOdo", g_h.oilChangeOdometerMi);
    g_prefs.putFloat("lastWarm", g_h.lastWarmupMinutes);
    g_prefs.putFloat("baseSoot", g_h.baseSootG);
    g_prefs.putFloat("baseDist", g_h.baseDistMi);
    g_prefs.putBytes("trigHist", g_h.triggerHist, sizeof(g_h.triggerHist));
    g_prefs.putUChar("trigCount", g_h.triggerCount);
    g_prefs.putUChar("trigHead", g_h.triggerHead);
    g_prefs.putBool("cycStart", g_h.cycleHadStart);
    g_prefs.putFloat("lastRate", g_h.lastRateGPerMi);
    g_dirty = false;
}

const HealthState &healthGet() { return g_h; }

void healthOilChangeNow(float currentOdometerMi) {
    g_h.oilChangeOdometerMi = isnan(currentOdometerMi) ? 0.0f : currentOdometerMi;
    g_h.regensSinceOil = 0;
    healthSave();   // "I just did this" is worth an immediate write, not a batched one
}

// ---- derived readouts ----
static float medianOf(const float *arr, uint8_t n) {
    if (n == 0) return NAN;
    float tmp[HealthState::MAX_WARMUPS];
    memcpy(tmp, arr, n * sizeof(float));
    for (uint8_t i = 1; i < n; i++) {
        float key = tmp[i];
        int j = (int)i - 1;
        while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = key;
    }
    return (n % 2) ? tmp[n / 2] : (tmp[n / 2 - 1] + tmp[n / 2]) / 2.0f;
}
float healthWarmupMedianMin() { return medianOf(g_h.warmupMinutes, g_h.warmupCount); }

float healthAvgRegenIntervalMi() {
    float sum = 0;
    int n = 0;
    for (uint8_t i = 0; i < g_h.regenCount; i++) {
        float mi = g_h.regenHistory[i].milesSincePrev;
        if (!isnan(mi)) { sum += mi; n++; }
    }
    return n ? sum / n : NAN;
}

float healthRegenTriggerG() {
    if (g_h.triggerCount == 0) return DEFAULT_TRIGGER_G;
    float m = medianOf(g_h.triggerHist, g_h.triggerCount);
    return fminf(TRIGGER_MAX_G, fmaxf(TRIGGER_MIN_G, m));
}

// This cycle's soot rate since the baseline, NAN until there are enough miles.
static float liveRate(float soot, float dist) {
    if (isnan(soot) || isnan(dist) || isnan(g_h.baseSootG)) return NAN;
    float span = dist - g_h.baseDistMi;
    if (span < MIN_LIVE_SPAN_MI) return NAN;
    float rate = (soot - g_h.baseSootG) / span;
    return rate >= MIN_LIVE_RATE ? rate : NAN;
}

// Estimates, best first:
//  1. a soot rate: this cycle's once it has enough miles, else the previous cycle's;
//     then (trigger - soot now) / rate. Follows how the car is actually being driven.
//  2. fallback: the historical average regen interval minus miles since the last regen.
// Computed into locals and stored at the end, so the screen/web tasks never read a
// half-updated result.
static void updateEstimate(float soot, float dist) {
    float miles = NAN, usedRate = NAN;
    bool fromRate = false;
    if (!isnan(dist)) {
        usedRate = liveRate(soot, dist);
        if (isnan(usedRate)) usedRate = g_h.lastRateGPerMi;
        if (!isnan(usedRate) && !isnan(soot)) {
            miles = fminf(MAX_ESTIMATE_MI, fmaxf(0.0f, (healthRegenTriggerG() - soot) / usedRate));
            fromRate = true;
        } else {
            usedRate = NAN;
            float avg = healthAvgRegenIntervalMi();
            if (!isnan(avg)) miles = fminf(MAX_ESTIMATE_MI, fmaxf(0.0f, avg - dist));
        }
    }
    g_h.estRateGPerMi = usedRate;
    g_h.estFromLiveRate = fromRate;
    g_h.estMilesToRegen = miles;
}

int healthRegensToday() {
    double now = unixNow();
    if (isnan(now) || now < 1.0) return -1;
    time_t t = (time_t)now;
    struct tm tmv;
    localtime_r(&t, &tmv);
    double dayStart = now - (tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec);
    int cnt = 0;
    for (uint8_t i = 0; i < g_h.regenCount; i++)
        if (g_h.regenHistory[i].wallClock >= dayStart) cnt++;
    return cnt;
}
int healthRegensThisWeek() {
    double now = unixNow();
    if (isnan(now) || now < 1.0) return -1;
    double weekAgo = now - 7.0 * 86400.0;
    int cnt = 0;
    for (uint8_t i = 0; i < g_h.regenCount; i++)
        if (g_h.regenHistory[i].wallClock >= weekAgo) cnt++;
    return cnt;
}

// ---- edge-triggered alerts: a "condition" latch (current level) plus a
// "pending" latch (delivered exactly once per rising edge) ----
static bool s_fastRegenCond = false, s_fastRegenPending = false;
static bool s_oilCond = false, s_oilPending = false;
static bool s_slowWarmupCond = false, s_slowWarmupPending = false;

static void evalFastRegen() {
    bool cond = false;
    if (g_h.regenCount >= 3) {
        cond = true;
        for (uint8_t k = 0; k < 3; k++) {
            uint8_t idx = (uint8_t)((g_h.regenHead + HealthState::MAX_REGENS - 1 - k) % HealthState::MAX_REGENS);
            float mi = g_h.regenHistory[idx].milesSincePrev;
            if (isnan(mi) || mi >= FAST_REGEN_THRESHOLD_MI) { cond = false; break; }
        }
    }
    if (cond && !s_fastRegenCond) s_fastRegenPending = true;
    s_fastRegenCond = cond;
}
static void evalOilReminder() {
    bool cond = g_h.regensSinceOil >= OIL_REMINDER_REGENS;
    if (cond && !s_oilCond) s_oilPending = true;
    s_oilCond = cond;
}
static void evalSlowWarmup() {
    float median = healthWarmupMedianMin();
    bool cond = g_h.warmupCount >= SLOW_WARMUP_MIN_SAMPLES && !isnan(median) &&
                !isnan(g_h.lastWarmupMinutes) && g_h.lastWarmupMinutes > median * SLOW_WARMUP_RATIO;
    if (cond && !s_slowWarmupCond) s_slowWarmupPending = true;
    s_slowWarmupCond = cond;
}

bool healthFastRegenAlert() { if (s_fastRegenPending) { s_fastRegenPending = false; return true; } return false; }
bool healthOilReminderAlert() { if (s_oilPending) { s_oilPending = false; return true; } return false; }
bool healthSlowWarmupAlert() { if (s_slowWarmupPending) { s_slowWarmupPending = false; return true; } return false; }

// ---- per-row update ----
static bool s_warming = false;
static float s_lastDist = NAN;   // previous row's dpf_dist_since_regen_mi
static uint8_t s_resetRows = 0;  // consecutive rows that look like a completed-regen reset
static uint32_t s_warmStartMs = 0;

void healthUpdate(const float *values, size_t count, RegenEvent lastEvent) {
    resolveColumns();
    bool changed = false;

    float batt = valAt(values, count, s_colBatt);
    if (!isnan(batt)) g_h.lastBatteryV = batt;   // live only, never persisted on its own

    float soot = valAt(values, count, s_colSoot);
    float dist = valAt(values, count, s_colDist);
    // dist_since_regen dropping to ~0 with a near-empty filter, for two rows running, means a
    // regen completed: the new cycle starts here. A reset while the board was off leaves the
    // previous cycle's baseline, which is also near 0 g / 0 mi, so the estimate is still close.
    if (!isnan(dist)) {
        bool dropped = (!isnan(s_lastDist) && dist + 0.5f < s_lastDist) || dist + 0.5f < g_h.baseDistMi;
        bool looksReset = dist < RESET_MAX_DIST_MI && (isnan(soot) || soot < RESET_MAX_SOOT_G);
        s_resetRows = (looksReset && (dropped || s_resetRows > 0)) ? (uint8_t)(s_resetRows + 1) : 0;
        if (s_resetRows == RESET_CONFIRM_ROWS) {
            float r = liveRate(soot, s_lastDist);   // carry this cycle's rate into the next one
            if (!isnan(r)) g_h.lastRateGPerMi = r;
            g_h.baseDistMi = dist;
            g_h.baseSootG = NAN;   // taken from the first soot reading below
            g_h.cycleHadStart = false;
            changed = true;
        }
        s_lastDist = dist;
    }
    // Soot is polled at a different rate from distance, so it can be missing on the reset row.
    if (isnan(g_h.baseSootG) && !isnan(soot) && !isnan(dist)) {
        g_h.baseSootG = (dist - g_h.baseDistMi < 2.0f) ? soot : 0.0f;
        changed = true;
    }

    if (lastEvent.type == RegenEvent::START) {
        // Only the first start of a cycle teaches the trigger: later ones are retries after an
        // abort, at whatever lower level the partial burn left.
        if (!g_h.cycleHadStart && !isnan(soot)) {
            g_h.triggerHist[g_h.triggerHead] = soot;
            g_h.triggerHead = (uint8_t)((g_h.triggerHead + 1) % HealthState::MAX_TRIGGERS);
            if (g_h.triggerCount < HealthState::MAX_TRIGGERS) g_h.triggerCount++;
        }
        g_h.cycleHadStart = true;
        HealthState::RegenRecord &r = g_h.regenHistory[g_h.regenHead];
        r.odometerMi = valAt(values, count, s_colOdo);
        r.milesSincePrev = valAt(values, count, s_colDist);
        r.interrupted = false;
        r.wallClock = unixNow();
        g_h.regenHead = (uint8_t)((g_h.regenHead + 1) % HealthState::MAX_REGENS);
        if (g_h.regenCount < HealthState::MAX_REGENS) g_h.regenCount++;
        g_h.regensSinceOil++;
        changed = true;
        evalFastRegen();
    } else if (lastEvent.type == RegenEvent::INTERRUPTED) {
        if (g_h.regenCount > 0) {
            uint8_t lastIdx = (uint8_t)((g_h.regenHead + HealthState::MAX_REGENS - 1) % HealthState::MAX_REGENS);
            if (!g_h.regenHistory[lastIdx].interrupted) { g_h.regenHistory[lastIdx].interrupted = true; changed = true; }
        }
        // An aborted burn drops soot without resetting the distance. Keep the rate seen so far
        // and measure on from here, so the drop isn't averaged in as slow accumulation.
        if (!isnan(dist)) {
            float r = liveRate(soot, dist);
            if (!isnan(r)) g_h.lastRateGPerMi = r;
            g_h.baseDistMi = dist;
            g_h.baseSootG = NAN;
            changed = true;
        }
    }

    float coolant = valAt(values, count, s_colCoolant);
    if (!isnan(coolant)) {
        if (!s_warming && coolant < WARMUP_COLD_C) {
            s_warming = true;
            s_warmStartMs = millis();
        } else if (s_warming && coolant >= WARMUP_DONE_C) {
            float mins = (millis() - s_warmStartMs) / 60000.0f;
            g_h.warmupMinutes[g_h.warmupHead] = mins;
            g_h.warmupHead = (uint8_t)((g_h.warmupHead + 1) % HealthState::MAX_WARMUPS);
            if (g_h.warmupCount < HealthState::MAX_WARMUPS) g_h.warmupCount++;
            g_h.lastWarmupMinutes = mins;
            s_warming = false;
            changed = true;
            evalSlowWarmup();
        }
    }

    updateEstimate(soot, dist);
    evalOilReminder();   // level check, cheap, re-evaluated every row (no flash write on its own)

    if (changed) healthSave();
}
