#include "regen_watch.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const int STREAK_ROWS = 3;
static const uint32_t GAP_MS = 120000;   // signal lost this long mid-regen = interrupted
static const uint32_t TICK_MS = 30000;   // status ping while a regen is running

static void appendClock(char *buf, size_t cap, double wallClock) {
    if (wallClock < 1735689600.0) return;   // clock not set (before 2025-01-01): omit rather than show a stale time
    time_t t = (time_t)wallClock;
    struct tm tmv;
    localtime_r(&t, &tmv);
    size_t n = strlen(buf);
    strftime(buf + n, cap - n, " at %H:%M", &tmv);
}

RegenEvent RegenWatch::update(uint32_t nowMs, double wallClock, float active, float egtC, float sootG, float distMi, float pressureHpa) {
    RegenEvent ev;
    if (isnan(active)) return ev;

    // Data resumed after a long gap while a regen was open: the car was
    // switched off (or the link dropped) mid-regen, so there is no clean end.
    if (inRegen_ && nowMs - lastValidMs_ > GAP_MS) {
        snprintf(ev.text, sizeof(ev.text),
                 "DPF regeneration was interrupted (engine stopped or signal lost)\nran about %u min, peak EGT %.0f C",
                 (unsigned)((lastValidMs_ - startMs_) / 60000UL), peakEgt_);
        appendClock(ev.text, sizeof(ev.text), startWall_);
        ev.type = RegenEvent::INTERRUPTED;
        inRegen_ = false;
        activeStreak_ = inactiveStreak_ = 0;
        lastValidMs_ = nowMs;
        return ev;
    }
    lastValidMs_ = nowMs;
    if (!isnan(sootG)) lastSoot_ = sootG;
    if (inRegen_ && !isnan(egtC) && egtC > peakEgt_) peakEgt_ = egtC;

    if (active > 0.5f) { activeStreak_++; inactiveStreak_ = 0; }
    else               { inactiveStreak_++; activeStreak_ = 0; }

    if (!inRegen_ && activeStreak_ >= STREAK_ROWS) {
        inRegen_ = true;
        startMs_ = nowMs;
        lastTickMs_ = nowMs;
        peakEgt_ = isnan(egtC) ? 0 : egtC;
        sootStart_ = isnan(sootG) ? lastSoot_ : sootG;
        distStart_ = isnan(distMi) ? 0 : distMi;
        startWall_ = wallClock;
        int n = snprintf(ev.text, sizeof(ev.text),
                 "DPF regeneration started\nsoot %.1f g, %.1f mi since the last regen, EGT %.0f C",
                 sootStart_, distStart_, isnan(egtC) ? 0.0f : egtC);
        if (!isnan(pressureHpa) && n > 0 && n < (int)sizeof(ev.text))
            snprintf(ev.text + n, sizeof(ev.text) - n, ", DPF pressure %.0f hPa", pressureHpa);
        appendClock(ev.text, sizeof(ev.text), startWall_);
        ev.type = RegenEvent::START;
    } else if (inRegen_ && inactiveStreak_ >= STREAK_ROWS) {
        inRegen_ = false;
        snprintf(ev.text, sizeof(ev.text),
                 "DPF regeneration finished\ntook about %u min, peak EGT %.0f C\nsoot %.1f g -> %.1f g",
                 (unsigned)((nowMs - startMs_) / 60000UL), peakEgt_, sootStart_, isnan(sootG) ? lastSoot_ : sootG);
        appendClock(ev.text, sizeof(ev.text), wallClock);
        ev.type = RegenEvent::END;
    } else if (inRegen_ && nowMs - lastTickMs_ >= TICK_MS) {
        lastTickMs_ = nowMs;
        unsigned mins = (nowMs - startMs_) / 60000UL;
        unsigned secs = ((nowMs - startMs_) / 1000UL) % 60UL;
        int n = snprintf(ev.text, sizeof(ev.text),
                 "DPF regen still running -- %u:%02u so far\nEGT %.0f C, soot %.1f g",
                 mins, secs, isnan(egtC) ? 0.0f : egtC, isnan(sootG) ? lastSoot_ : sootG);
        if (!isnan(pressureHpa) && n > 0 && n < (int)sizeof(ev.text))
            n += snprintf(ev.text + n, sizeof(ev.text) - n, ", DPF pressure %.0f hPa", pressureHpa);
        appendClock(ev.text, sizeof(ev.text), wallClock);
        ev.type = RegenEvent::TICK;
    }
    return ev;
}
