#pragma once
#include <math.h>
#include <stdint.h>

// Watches the decoded DPF regen flag and produces a one-line-per-fact message
// when a regeneration starts or ends. Pure logic (no Arduino types) so it is
// unit-tested natively. Twin of android/core/.../RegenWatch.kt.
//
// The ECU drops the regen flag while the car stands at idle (a drive-through,
// traffic) or when the engine is switched off, and picks the same regen up again
// once the car drives on (session_59, 2026-10-25). That is reported as PAUSED and
// then RESUMED, and only becomes INTERRUPTED after 3 min of driving or 20 min in
// all without it coming back.
struct RegenEvent {
    enum Type { NONE, START, END, INTERRUPTED, TICK, PAUSED, RESUMED } type = NONE;
    char text[224] = "";
};

class RegenWatch {
public:
    // Call once per logged row. Values may be NAN when a signal is missing.
    // A change must hold for a few rows before it counts (debounce).
    // wallClock: seconds since epoch for this row (NAN/0 if the clock isn't set yet).
    // speedKmh: NAN when unknown, which keeps the old behaviour (a dropped flag = finished).
    RegenEvent update(uint32_t nowMs, double wallClock, float active, float egtC, float sootG, float distMi, float pressureHpa,
                      float speedKmh = NAN);

    bool active() const { return inRegen_; }               // running or paused
    bool paused() const { return inRegen_ && paused_; }

private:
    void pause(uint32_t atMs);
    void close(RegenEvent &ev, double wallClock, float soot, bool emptyFilter);

    bool inRegen_ = false, paused_ = false, haveSoot_ = false;
    int activeStreak_ = 0, inactiveStreak_ = 0;
    uint32_t startMs_ = 0, lastValidMs_ = 0, lastTickMs_ = 0, pausedAtMs_ = 0, movingWhilePausedMs_ = 0;
    double startWall_ = 0;
    float peakEgt_ = 0, sootStart_ = 0, distStart_ = 0;
    float lastSoot_ = 0;
};
