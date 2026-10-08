#include "touch.h"
#include "../board/board.h"

static const uint32_t POLL_MS = 20;          // ~50 Hz
static const uint32_t TAP_MAX_MS = 700;
static const uint32_t UP_DEBOUNCE_MS = 60;   // finger absent this long = released
static const int TAP_MAX_MOVE = 30;

static uint32_t lastPoll = 0, lastActivity = 0, downAt = 0, lastSeen = 0;
static bool down = false, longFired = false;
static int16_t sx, sy, lx, ly;

bool touchBegin() { lastActivity = millis(); return true; }
uint32_t touchLastActivityMs() { return lastActivity; }

// Tap = short touch that stays put. Long presses and swipes are not used on this board.
TouchEvent touchPoll() {
  TouchEvent ev = {TouchEvent::NONE, 0, 0};
  uint32_t now = millis();
  if (now - lastPoll < POLL_MS) return ev;
  lastPoll = now;
  int16_t x = 0, y = 0;
  bool pressed = false;
  if (!boardTouchRead(x, y, pressed)) return ev;
  if (pressed) {
    lastActivity = lastSeen = now;
    if (!down) { down = true; longFired = false; downAt = now; sx = lx = x; sy = ly = y; }
    else { lx = x; ly = y; }
    // Long press: held still for 0.7 s -> one LONG event while still down; release then produces nothing.
    if (!longFired && now - downAt >= TAP_MAX_MS && abs(lx - sx) < TAP_MAX_MOVE && abs(ly - sy) < TAP_MAX_MOVE) { longFired = true; ev = {TouchEvent::LONG, sx, sy}; }
    return ev;
  }
  if (!down || now - lastSeen < UP_DEBOUNCE_MS) return ev;
  down = false;
  lastActivity = now;
  if (longFired) return ev;
  if (abs(lx - sx) < TAP_MAX_MOVE && abs(ly - sy) < TAP_MAX_MOVE && lastSeen - downAt < TAP_MAX_MS) ev = {TouchEvent::TAP, sx, sy};
  return ev;
}
