#pragma once
#include <stdint.h>

// "DPF pressure high at idle", the possible-blockage sign. Twin of
// android/core/.../IdleBlockage.kt.
//
// Idle DPF pressure rises with the soot load: a normal 10-18 g filter idles at
// about 22 hPa, and during a regen idle pressure is 20+ hPa most of the time, so
// a single-row "20 hPa at idle" test fired on every loaded filter and through
// every regen. Now it needs a warm engine at idle, no regen running (or paused)
// and none in the last 10 min, and the threshold held for a minute. Across the
// logs up to 2026-11-07 (123 sessions) that never fires.
class IdleBlockageWatch {
public:
    // Values may be NAN. regenBusy: a regen is running or paused.
    bool update(uint32_t nowMs, float rpm, float speedKmh, float dpHpa, float coolantC, bool regenBusy);
    bool flagged() const { return flagged_; }

private:
    bool flagged_ = false, aboveValid_ = false, regenSeen_ = false;
    uint32_t aboveSince_ = 0, lastRegenMs_ = 0;
};
