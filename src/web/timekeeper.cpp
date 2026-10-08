#include "timekeeper.h"
#include "../config.h"
#include <sys/time.h>
#include <time.h>
#include <Wire.h>
#include <esp_sntp.h>

static bool gRtc = false;   // DS3231 answered at 0x68
static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static uint8_t toBcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool rtcRead(time_t &out) {
    Wire.beginTransmission(0x68); Wire.write(0);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x68, 7) != 7) return false;
    uint8_t b[7]; for (auto &x : b) x = Wire.read();
    struct tm t = {};
    t.tm_sec = bcd2(b[0] & 0x7F); t.tm_min = bcd2(b[1]); t.tm_hour = bcd2(b[2] & 0x3F);
    t.tm_mday = bcd2(b[4]); t.tm_mon = bcd2(b[5] & 0x1F) - 1; t.tm_year = 100 + bcd2(b[6]);
    setenv("TZ", "UTC0", 1); tzset();
    out = mktime(&t);                     // the RTC always holds UTC
    setenv("TZ", TZ_RULE, 1); tzset();
    return true;
}

static void rtcWrite(time_t utc) {
    if (!gRtc) return;
    struct tm t; gmtime_r(&utc, &t);
    Wire.beginTransmission(0x68); Wire.write(0);
    Wire.write(toBcd(t.tm_sec)); Wire.write(toBcd(t.tm_min)); Wire.write(toBcd(t.tm_hour));
    Wire.write(toBcd(t.tm_wday + 1)); Wire.write(toBcd(t.tm_mday)); Wire.write(toBcd(t.tm_mon + 1));
    Wire.write(toBcd(t.tm_year % 100));
    Wire.endTransmission();
    Serial.println("RTC: DS3231 updated");
}

static char gSrc[8] = "";
static bool gPending = false;
static double gSetUnix = 0;
static uint32_t gSetMs = 0;

bool timeValid() { return time(nullptr) > 1735689600; }  // after 2025-01-01

double unixNow() {
    if (!timeValid()) return NAN;
    timeval tv;
    gettimeofday(&tv, nullptr);
    return (double)tv.tv_sec + tv.tv_usec / 1e6;
}

const char *timeSource() { return gSrc; }

static void noteSet(const char *src) {
    strncpy(gSrc, src, sizeof(gSrc) - 1);
    gSetUnix = unixNow();
    gSetMs = millis();
    gPending = true;
}

bool timeSetFromPhone(uint64_t unixMs) {
    // Accept unless NTP already set the clock; a DS3231 seed ("rtc") may be stale, so the phone wins.
    if ((timeValid() && strcmp(gSrc, "rtc") != 0 && strcmp(gSrc, "") != 0) || unixMs < 1735689600000ULL) return false;
    timeval tv;
    tv.tv_sec = (time_t)(unixMs / 1000ULL);
    tv.tv_usec = (suseconds_t)((unixMs % 1000ULL) * 1000ULL);
    settimeofday(&tv, nullptr);
    noteSet("phone");
    rtcWrite(tv.tv_sec);
    return true;
}

// Applied at boot, independent of NTP: the clock can also be set from the
// phone (no internet), and localtime_r() must still apply the DST rule then.
void timeInitTz() {
    setenv("TZ", TZ_RULE, 1);
    tzset();
}

void timeStartNtp() { configTzTime(TZ_RULE, "pool.ntp.org", "time.google.com"); }

void timeRtcBegin() {
#if RTC_DS3231_ENABLED
#if RTC_SDA_PIN >= 0
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN, 100000);
#endif
    time_t t;
    if (!rtcRead(t)) { Serial.println("RTC: no DS3231 found (clock will come from NTP/phone)"); return; }
    gRtc = true;
    if (t > 1735689600) {   // battery-backed value looks sane (after 2025-01-01)
        timeval tv = {t, 0};
        settimeofday(&tv, nullptr);
        noteSet("rtc");
    } else Serial.println("RTC: DS3231 present but unset");
#endif
}

void timePoll(SessionStore &store) {
    if (timeValid() && (!gSrc[0] || (!strcmp(gSrc, "rtc") && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED))) {
        noteSet("ntp");
        rtcWrite((time_t)gSetUnix);
    }
    if (gPending) {
        gPending = false;
        store.queueRaw(gSetMs, "TIME", gSrc, String(gSetUnix, 3));
        Serial.printf("Clock set from %s: %.0f (unix)\n", gSrc, gSetUnix);
    }
}
