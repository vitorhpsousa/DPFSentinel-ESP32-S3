#pragma once
#include <Arduino.h>
#include "../logging/session_store.h"

// Wall-clock time without an RTC. The clock is set at most once per boot, from
// the internet (when the home Wi-Fi is joined) or from the phone that opens
// the dashboard, and it is best-effort: with neither, rows simply carry no
// wall-clock time and nothing else changes. Each time it is set, a TIME marker
// (millis, source, unix time) goes into the raw log, so every earlier row of
// that boot can be dated afterwards (tools/date_session.py).
bool timeValid();
double unixNow();                       // NAN until the clock has been set
const char *timeSource();               // "", "rtc", "ntp" or "phone"
bool timeSetFromPhone(uint64_t unixMs); // accepted unless NTP already set the clock this boot
void timeRtcBegin();                    // optional DS3231 at 0x68: seeds the clock at boot, written back on NTP/phone sync
void timeInitTz();                      // call once at boot: sets the TZ_RULE (DST-aware) for localtime
void timeStartNtp();                    // non-blocking; runs in the background
void timePoll(SessionStore &store);     // call once a second: emits the marker
