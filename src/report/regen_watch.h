#pragma once
#include <stdint.h>

// Watches the decoded DPF regen flag and produces a one-line-per-fact message
// when a regeneration starts or ends. Pure logic (no Arduino types) so it is
// unit-tested natively by replaying a real regen from the Car Scanner log.
struct RegenEvent {
    enum Type { NONE, START, END, INTERRUPTED, TICK } type = NONE;
    char text[224] = "";
};

class RegenWatch {
public:
    // Call once per logged row. Values may be NAN when a signal is missing.
    // A change must hold for a few rows before it counts (debounce).
    // wallClock: seconds since epoch for this row (NAN/0 if the clock isn't set yet).
    RegenEvent update(uint32_t nowMs, double wallClock, float active, float egtC, float sootG, float distMi, float pressureHpa);

private:
    bool inRegen_ = false;
    int activeStreak_ = 0, inactiveStreak_ = 0;
    uint32_t startMs_ = 0, lastValidMs_ = 0, lastTickMs_ = 0;
    double startWall_ = 0;
    float peakEgt_ = 0, sootStart_ = 0, distStart_ = 0;
    float lastSoot_ = 0;
};
