#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "../report/regen_watch.h"

// Engine-health tracking equivalent to the Pi's design, PERSISTED across
// reboots via the ESP32 Preferences library (NVS, namespace "health"):
// regen interval/count with a 3-fast-regens alert, regens since an
// oil-change reference, warm-up time to 80 C with a rolling median, and the
// last known battery voltage. Thresholds are guesses (no manufacturer spec
// found) lifted from docs/ix35-known-issues.md
// metrics #2 (regen interval), #7 (warm-up time) and #13 (oil dilution
// exposure) — calibrate against this car's own logs once there's enough
// history.
//
// Call healthBegin() once at boot (after Serial/Preferences are available)
// and healthUpdate() once per assembled logger row, right after
// regenWatch.update() (see main.cpp's per-row block).
struct HealthState {
    struct RegenRecord {
        float odometerMi = NAN;       // odometer at regen start (NAN = unknown)
        float milesSincePrev = NAN;   // dpf_dist_since_regen_mi at start, i.e. the interval
        bool interrupted = false;     // set if regenWatch later reported INTERRUPTED
        double wallClock = 0;         // unix seconds at start; 0 = clock wasn't set yet
    };
    static const uint8_t MAX_REGENS = 20;
    RegenRecord regenHistory[MAX_REGENS];
    uint8_t regenCount = 0;   // number of valid entries (caps at MAX_REGENS)
    uint8_t regenHead = 0;    // ring buffer next-write index

    uint16_t regensSinceOil = 0;
    float oilChangeOdometerMi = 0;   // 0 = unset (no reference recorded yet)

    static const uint8_t MAX_WARMUPS = 10;
    float warmupMinutes[MAX_WARMUPS] = {0};
    uint8_t warmupCount = 0, warmupHead = 0;
    float lastWarmupMinutes = NAN;

    float lastBatteryV = NAN;   // live reading, not persisted
};

void healthBegin();   // load from Preferences; call once at boot

// Call once per assembled logger row. lastEvent is the RegenEvent returned
// by that same row's regenWatch.update() call (NONE most rows). Updates
// state and calls healthSave() internally only when something actually
// changed, so it doesn't wear the flash every second.
void healthUpdate(const float *values, size_t count, RegenEvent lastEvent);

const HealthState &healthGet();

// "I just changed the oil, reset the counter now": records the current
// odometer as the new reference and zeroes regensSinceOil, persisted
// immediately. Wired to the 'O' serial command in main.cpp — there is no
// touchscreen keyboard on this board to enter a value another way.
void healthOilChangeNow(float currentOdometerMi);

// Derived readouts for the Health UI page (ui/pages.cpp).
float healthAvgRegenIntervalMi();   // mean of milesSincePrev over the stored history; NAN if none yet
float healthWarmupMedianMin();      // rolling median warm-up time; NAN if no samples yet
int healthRegensToday();            // -1 if the clock isn't set yet
int healthRegensThisWeek();         // -1 if the clock isn't set yet

// Edge-triggered: each returns true at most once per rising edge of its
// condition, then stays false until the condition clears and re-arms, so a
// caller (main.cpp) can Telegram exactly one message per episode.
bool healthFastRegenAlert();    // last 3 regens all closer together than the fast-regen threshold
bool healthOilReminderAlert();  // regensSinceOil has reached the reminder threshold
bool healthSlowWarmupAlert();   // last warm-up time is far above the rolling median
