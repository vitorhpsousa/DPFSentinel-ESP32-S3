#include "regen_watch.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const int STREAK_ROWS = 3;
static const uint32_t GAP_MS = 120000;           // no data this long mid-regen = engine off or link lost: paused
static const uint32_t TICK_MS = 30000;           // status ping while a regen is running
static const float STILL_KMH = 3.0f;             // at or below this the car counts as standing
static const float EMPTY_SOOT_G = 3.0f;          // a flag drop at or below this soot is a real finish
static const uint32_t PAUSE_DRIVE_MS = 180000;   // driving this long without it resuming = stopped
static const uint32_t PAUSE_MAX_MS = 1200000;    // ... or this long paused in all

static void appendClock(char *buf, size_t cap, double wallClock) {
    if (wallClock < 1735689600.0) return;   // clock not set (before 2025-01-01): omit rather than show a stale time
    time_t t = (time_t)wallClock;
    struct tm tmv;
    localtime_r(&t, &tmv);
    size_t n = strlen(buf);
    strftime(buf + n, cap - n, " at %H:%M", &tmv);
}

void RegenWatch::pause(uint32_t atMs) {
    paused_ = true;
    pausedAtMs_ = atMs;
    movingWhilePausedMs_ = 0;
}

// Closes a paused regen that never came back: finished if the filter is (nearly) empty.
void RegenWatch::close(RegenEvent &ev, double wallClock, float soot, bool emptyFilter) {
    inRegen_ = false;
    paused_ = false;
    snprintf(ev.text, sizeof(ev.text), "%s\nran about %u min, peak EGT %.0f C\nsoot %.1f g -> %.1f g",
             emptyFilter ? "DPF regeneration finished" : "DPF regeneration stopped before finishing",
             (unsigned)((pausedAtMs_ - startMs_) / 60000UL), peakEgt_, sootStart_, soot);
    appendClock(ev.text, sizeof(ev.text), wallClock);
    ev.type = emptyFilter ? RegenEvent::END : RegenEvent::INTERRUPTED;
}

RegenEvent RegenWatch::update(uint32_t nowMs, double wallClock, float active, float egtC, float sootG, float distMi, float pressureHpa,
                              float speedKmh) {
    RegenEvent ev;
    if (isnan(active)) return ev;

    // Data resumed after a long gap while a regen was running: engine stopped or
    // the link dropped. The ECU usually carries the regen on once the car drives.
    if (inRegen_ && !paused_ && nowMs - lastValidMs_ > GAP_MS) {
        pause(lastValidMs_);
        snprintf(ev.text, sizeof(ev.text),
                 "DPF regeneration paused (engine stopped or signal lost)\nran about %u min, peak EGT %.0f C. It usually carries on once you drive again",
                 (unsigned)((pausedAtMs_ - startMs_) / 60000UL), peakEgt_);
        appendClock(ev.text, sizeof(ev.text), wallClock);
        ev.type = RegenEvent::PAUSED;
        activeStreak_ = inactiveStreak_ = 0;
        lastValidMs_ = nowMs;
        return ev;
    }
    uint32_t dt = nowMs - lastValidMs_;
    if (dt > 5000) dt = 5000;
    lastValidMs_ = nowMs;
    if (!isnan(sootG)) { lastSoot_ = sootG; haveSoot_ = true; }
    float soot = isnan(sootG) ? lastSoot_ : sootG;
    bool emptyFilter = haveSoot_ && soot <= EMPTY_SOOT_G;   // unknown soot never counts as empty
    if (inRegen_ && !isnan(egtC) && egtC > peakEgt_) peakEgt_ = egtC;

    if (active > 0.5f) { activeStreak_++; inactiveStreak_ = 0; }
    else               { inactiveStreak_++; activeStreak_ = 0; }

    if (paused()) {
        if (activeStreak_ >= STREAK_ROWS) {
            paused_ = false;
            lastTickMs_ = nowMs;
            snprintf(ev.text, sizeof(ev.text), "DPF regeneration resumed\nafter a %u min pause, soot %.1f g, EGT %.0f C",
                     (unsigned)((nowMs - pausedAtMs_) / 60000UL), soot, isnan(egtC) ? 0.0f : egtC);
            appendClock(ev.text, sizeof(ev.text), wallClock);
            ev.type = RegenEvent::RESUMED;
            return ev;
        }
        if (!isnan(speedKmh) && speedKmh > STILL_KMH) movingWhilePausedMs_ += dt;
        if (movingWhilePausedMs_ >= PAUSE_DRIVE_MS || nowMs - pausedAtMs_ >= PAUSE_MAX_MS) close(ev, wallClock, soot, emptyFilter);
        return ev;
    }

    if (!inRegen_ && activeStreak_ >= STREAK_ROWS) {
        inRegen_ = true;
        paused_ = false;
        startMs_ = nowMs;
        lastTickMs_ = nowMs;
        peakEgt_ = isnan(egtC) ? 0 : egtC;
        sootStart_ = soot;
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
        // Flag dropped while standing still with soot left: the ECU is holding the regen.
        bool standing = !isnan(speedKmh) && speedKmh <= STILL_KMH;
        if (standing && !emptyFilter) {
            pause(nowMs);
            snprintf(ev.text, sizeof(ev.text),
                     "DPF regeneration paused while the car is stopped\nran %u min so far, soot %.1f g. It should carry on once you drive off",
                     (unsigned)((nowMs - startMs_) / 60000UL), soot);
            appendClock(ev.text, sizeof(ev.text), wallClock);
            ev.type = RegenEvent::PAUSED;
            return ev;
        }
        inRegen_ = false;
        snprintf(ev.text, sizeof(ev.text),
                 "DPF regeneration finished\ntook about %u min, peak EGT %.0f C\nsoot %.1f g -> %.1f g",
                 (unsigned)((nowMs - startMs_) / 60000UL), peakEgt_, sootStart_, soot);
        appendClock(ev.text, sizeof(ev.text), wallClock);
        ev.type = RegenEvent::END;
    } else if (inRegen_ && nowMs - lastTickMs_ >= TICK_MS) {
        lastTickMs_ = nowMs;
        unsigned mins = (nowMs - startMs_) / 60000UL;
        unsigned secs = ((nowMs - startMs_) / 1000UL) % 60UL;
        int n = snprintf(ev.text, sizeof(ev.text),
                 "DPF regen still running -- %u:%02u so far\nEGT %.0f C, soot %.1f g",
                 mins, secs, isnan(egtC) ? 0.0f : egtC, soot);
        if (!isnan(pressureHpa) && n > 0 && n < (int)sizeof(ev.text))
            n += snprintf(ev.text + n, sizeof(ev.text) - n, ", DPF pressure %.0f hPa", pressureHpa);
        appendClock(ev.text, sizeof(ev.text), wallClock);
        ev.type = RegenEvent::TICK;
    }
    return ev;
}
