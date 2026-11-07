#include "dpf_monitor.h"
#include <math.h>

// Kept here rather than in config.h so the native tests build without the board
// config; the values match android/core/.../IdleBlockage.kt.
static const float THRESHOLD_HPA = 35.0f;        // held for HOLD_MS at warm idle
static const uint32_t HOLD_MS = 60000;
static const float WARM_C = 70.0f;
static const uint32_t AFTER_REGEN_MS = 600000;   // quiet for 10 min after a regen
static const float IDLE_RPM_CEILING = 1100.0f;
static const float MIN_RPM = 500.0f;             // below this the engine is stopping or cranking

bool IdleBlockageWatch::update(uint32_t nowMs, float rpm, float speedKmh, float dpHpa, float coolantC, bool regenBusy) {
    if (regenBusy) { lastRegenMs_ = nowMs; regenSeen_ = true; }
    bool idle = !isnan(rpm) && rpm >= MIN_RPM && rpm <= IDLE_RPM_CEILING && (isnan(speedKmh) || speedKmh < 1.0f);
    bool eligible = idle && !isnan(dpHpa) && !isnan(coolantC) && coolantC >= WARM_C &&
                    !regenBusy && (!regenSeen_ || nowMs - lastRegenMs_ >= AFTER_REGEN_MS);
    if (!eligible || dpHpa < THRESHOLD_HPA) {
        aboveValid_ = false;
        flagged_ = false;
        return false;
    }
    if (!aboveValid_) { aboveValid_ = true; aboveSince_ = nowMs; }
    flagged_ = nowMs - aboveSince_ >= HOLD_MS;
    return flagged_;
}
