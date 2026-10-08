#include "pages.h"
#include <math.h>
#include <string.h>
#include <WiFi.h>
#include "../board/board.h"
#include "aatext.h"

static const int W = UI_W, H = UI_H, M = 10;
static const float GREEN_MAX = 14.0f, AMBER_MAX = 17.0f, DARK_AT = 28.0f;

static GFXcanvas16 *s_cv = nullptr;
static PageId s_page = PAGE_SOOT;
static bool s_regenNow = false;
static bool s_dirty = true;        // page changed / needs full redraw
static uint32_t s_hash = 0;
static uint32_t s_lastDraw = 0;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static const uint16_t C_BG = 0x0000, C_FG = 0xFFFF;
static uint16_t C_LBL() { return rgb(0xcc, 0xcc, 0xcc); }
static uint16_t C_DIM() { return rgb(0x3a, 0x3f, 0x44); }
static uint16_t C_GRN() { return rgb(0x1f, 0xa5, 0x4c); }
static uint16_t C_AMB() { return rgb(0xe8, 0xa5, 0x48); }
static uint16_t C_RED() { return rgb(0xd0, 0x2c, 0x20); }

static uint16_t sootColor(float g) {
    if (isnan(g)) return C_DIM();
    if (g < GREEN_MAX) return C_GRN();
    if (g < AMBER_MAX) return C_AMB();
    float t = fminf(1.0f, (g - AMBER_MAX) / (DARK_AT - AMBER_MAX));
    return rgb(0xd0 + (0x8b - 0xd0) * t, 0x2c + (0x00 - 0x2c) * t, 0x20 + (0x00 - 0x20) * t);
}

// ---- text helpers (anti-aliased DejaVu via aatext; fit lists largest -> smallest) ----
static const AAFont *const F_LBL[] = {&aaR17, &aaR14};
static const AAFont *const F_VAL[] = {&aaR24, &aaR20, &aaR17};
static const AAFont *const F_BIG[] = {&aaB30, &aaB24, &aaB20};
static const AAFont *const F_HERO[] = {&aaB44, &aaB30, &aaB24};
template <size_t N> static const AAFont &fit(const char *t, int maxW, const AAFont *const (&l)[N]) { return aaFit(t, maxW, l, N); }
// Baseline-anchored so different fitted sizes on one row line up.
static void putB(const char *t, int x, int yb, const AAFont &f, uint16_t col) { aaDraw(s_cv, x, yb - f.ascent, t, f, col); }
static void rightB(const char *t, int xr, int yb, const AAFont &f, uint16_t col) { aaDrawRight(s_cv, xr, yb - f.ascent, t, f, col); }
static void centerB(const char *t, int cx, int yb, const AAFont &f, uint16_t col) { aaDrawCentered(s_cv, cx, yb - f.ascent, t, f, col); }
// Label (R17, light grey) at left, value (fitted R24..R17, white) right-aligned on the same baseline; row baseline = y + 21.
static void rowAt(int x0, int x1, int y, const char *label, const char *val, uint16_t col = C_FG) {
    const AAFont &lf = aaR17;
    putB(label, x0, y + 21, lf, C_LBL());
    int avail = x1 - x0 - aaMeasure(lf, label) - 10;
    rightB(val, x1, y + 21, fit(val, avail, F_VAL), col);
}
static void row(int y, const char *label, const char *val, uint16_t col = C_FG) { rowAt(M, W - M, y, label, val, col); }
static void title(const char *t) {
    putB(t, M, M + 22, aaB24, C_LBL());
    s_cv->drawFastHLine(M, M + 30, W - 2 * M, C_DIM());
}
static void fmtf(char *o, size_t n, float v, int dec, const char *unit) {
    if (isnan(v)) strlcpy(o, "-", n); else snprintf(o, n, "%.*f%s", dec, v, unit);
}
static void fmtDur(char *o, size_t n, uint32_t s) {
    if (s >= 86400) snprintf(o, n, "%lud %luh", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
    else snprintf(o, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60), (unsigned long)(s % 60));
}

// ---- pages ----
static void drawTrend(const PagesData &d) {
    title("SOOT TREND");
    float cur = NAN, mx = 0;
    for (int i = 239; i >= 0; i--) if (!isnan(d.sootHistory[i])) { cur = d.sootHistory[i]; break; }
    for (int i = 0; i < 240; i++) if (!isnan(d.sootHistory[i]) && d.sootHistory[i] > mx) mx = d.sootHistory[i];
    char b[24];
    fmtf(b, sizeof b, cur, 1, "g");
    rightB(b, W - M, M + 26, fit(b, 110, F_BIG), sootColor(cur));
    snprintf(b, sizeof b, "%u min/sample", d.sootHistoryStepMin);
    putB(b, 200, M + 20, aaR14, C_LBL());
    const int px0 = 40, px1 = W - M - 2, py0 = 48, py1 = H - 46;
    float ymax = 20.0f;
    while (ymax < mx + 1 && ymax < 60) ymax += 5;
    auto Y = [&](float v) { return py1 - (int)((py1 - py0) * fminf(fmaxf(v, 0), ymax) / ymax + 0.5f); };
    s_cv->drawRect(px0, py0, px1 - px0 + 1, py1 - py0 + 1, C_DIM());
    for (float t = 0; t <= ymax + 0.01f; t += 10) {
        snprintf(b, sizeof b, "%d", (int)t);
        int ty = Y(t);
        rightB(b, px0 - 5, ty + 5, aaR14, C_LBL());
        if (t > 0 && t < ymax) for (int x = px0 + 1; x < px1; x += 6) s_cv->drawPixel(x, ty, C_DIM());
    }
    s_cv->drawFastHLine(px0 + 1, Y(GREEN_MAX), px1 - px0 - 1, C_AMB());
    s_cv->drawFastHLine(px0 + 1, Y(AMBER_MAX), px1 - px0 - 1, C_RED());
    rightB("14", px1 - 3, Y(GREEN_MAX) + 15, aaR14, C_AMB());
    rightB("17", px1 - 3, Y(AMBER_MAX) - 2, aaR14, C_RED());
    const int pw = px1 - px0 - 2;
    int lx = -1, ly = 0; float lv = NAN;
    for (int i = 0; i < 240; i++) {
        float v = d.sootHistory[i];
        if (isnan(v)) { lx = -1; continue; }
        int x = px0 + 1 + i * (pw - 1) / 239, yy = Y(v);
        if (lx >= 0) {
            uint16_t c = sootColor((v + lv) * 0.5f);
            s_cv->drawLine(lx, ly, x, yy, c);
            s_cv->drawLine(lx, ly + 1, x, yy + 1, c);
        }
        lx = x; ly = yy; lv = v;
    }
    float spanH = d.sootHistoryStepMin * 240 / 60.0f;
    snprintf(b, sizeof b, "-%.0fh", spanH);
    putB(b, px0, py1 + 22, aaR17, C_LBL());
    snprintf(b, sizeof b, "-%.1fh", spanH / 2);
    centerB(b, (px0 + px1) / 2, py1 + 22, aaR17, C_LBL());
    rightB("now", px1, py1 + 22, aaR17, C_LBL());
}

static void drawTrip(const PagesData &d) {
    title("TODAY");
    char b[32];
    const int L0 = M, L1 = W / 2 - 8, R0 = W / 2 + 8, R1 = W - M;
    int y = 50;
    fmtf(b, sizeof b, d.tripMiles, 1, " mi");
    centerB(b, (L0 + L1) / 2, y + 40, fit(b, L1 - L0, F_HERO), C_FG);
    centerB("DRIVEN", (L0 + L1) / 2, y + 66, aaR17, C_LBL()); y += 78;
    fmtf(b, sizeof b, d.tripSootStart, 1, "g"); rowAt(L0, L1, y, "Soot start", b); y += 34;
    fmtf(b, sizeof b, d.tripSootPeak, 1, "g"); rowAt(L0, L1, y, "Soot peak", b, sootColor(d.tripSootPeak)); y += 34;
    fmtf(b, sizeof b, d.maxSpeed, 0, " km/h"); rowAt(L0, L1, y, "Max speed", b);
    s_cv->drawFastVLine(W / 2, 50, H - 50 - 30, C_DIM());
    y = 56;
    snprintf(b, sizeof b, "%u", d.regenCount); rowAt(R0, R1, y, "Regens", b); y += 40;
    if (d.lastRegenEndMs == 0) {
        rowAt(R0, R1, y, "Last regen", "none");
    } else {
        uint32_t ago = (millis() - d.lastRegenEndMs) / 1000;
        if (ago < 3600) snprintf(b, sizeof b, "%lum ago", (unsigned long)(ago / 60));
        else snprintf(b, sizeof b, "%luh%02lum ago", (unsigned long)(ago / 3600), (unsigned long)(ago % 3600 / 60));
        rowAt(R0, R1, y, "Last", b); y += 40;
        snprintf(b, sizeof b, "%u min", d.lastRegenMinutes); rowAt(R0, R1, y, "Duration", b); y += 40;
        fmtf(b, sizeof b, d.lastRegenPeakEgt, 0, "\xC2\xB0" "C"); rowAt(R0, R1, y, "Peak EGT", b);
    }
}

static void drawHealth(const UiState &ui, const PagesData &d) {
    title("HEALTH");
    char b[40];
    const int L0 = M, L1 = W / 2 - 8, R0 = W / 2 + 8, R1 = W - M;
    int y = 50;
    if (d.healthRegensToday < 0) rowAt(L0, L1, y, "Regens today", "-");
    else { snprintf(b, sizeof b, "%d", d.healthRegensToday); rowAt(L0, L1, y, "Regens today", b); }
    y += 34;
    if (d.healthRegensThisWeek < 0) rowAt(L0, L1, y, "This week", "-");
    else { snprintf(b, sizeof b, "%d", d.healthRegensThisWeek); rowAt(L0, L1, y, "This week", b); }
    y += 34;
    fmtf(b, sizeof b, ui.sinceRegen, 1, " mi"); rowAt(L0, L1, y, "Since last", b); y += 34;
    fmtf(b, sizeof b, d.healthAvgRegenIntervalMi, 0, " mi avg"); rowAt(L0, L1, y, "Avg interval", b); y += 34;
    snprintf(b, sizeof b, "%u", d.healthRegensSinceOil); rowAt(L0, L1, y, "Since oil", b);

    s_cv->drawFastVLine(W / 2, 50, H - 50 - 30, C_DIM());
    y = 50;
    if (d.healthOilChangeOdometerMi <= 0.0f) rowAt(R0, R1, y, "Oil set at", "not set");
    else { fmtf(b, sizeof b, d.healthOilChangeOdometerMi, 0, " mi"); rowAt(R0, R1, y, "Oil set at", b); }
    y += 34;
    fmtf(b, sizeof b, d.healthLastWarmupMin, 1, " min"); rowAt(R0, R1, y, "Warm-up", b); y += 34;
    fmtf(b, sizeof b, d.healthWarmupMedianMin, 1, " min"); rowAt(R0, R1, y, "Median", b); y += 34;
    fmtf(b, sizeof b, d.batteryV, 2, " V");
    rowAt(R0, R1, y, "Voltage", b, (!isnan(d.batteryV) && d.batteryV < 12.2f) ? C_AMB() : C_FG);
}

static void drawSystem(const PagesData &d) {
    title("SYSTEM");
    char b[40];
    const int L0 = M, L1 = W / 2 - 8, R0 = W / 2 + 8, R1 = W - M;
    int y = 50;
    bool wc = WiFi.status() == WL_CONNECTED;
    row(y, "WiFi", wc ? WiFi.SSID().c_str() : "offline"); y += 32;
    row(y, "IP", wc ? WiFi.localIP().toString().c_str() : "-"); y += 40;
    s_cv->drawFastHLine(M, y - 6, W - 2 * M, C_DIM());
    const int y0 = y, step = 38;
    rowAt(L0, L1, y, "BLE", d.bleLinked ? "linked" : "no link", d.bleLinked ? C_GRN() : C_RED()); y += step;
    snprintf(b, sizeof b, "%s %luMB", d.sdBackend[0] ? d.sdBackend : "none", (unsigned long)d.sdFreeMB);
    rowAt(L0, L1, y, "Storage", b); y += step;
    snprintf(b, sizeof b, "#%lu", (unsigned long)d.bootNumber); rowAt(L0, L1, y, "Session", b); y += step;
    rowAt(L0, L1, y, "Clock", d.clockText[0] ? d.clockText : "-", d.clockSynced ? C_FG : C_AMB());
    y = y0;
    rowAt(R0, R1, y, "Time sync", d.clockSynced ? "synced" : "NOT synced", d.clockSynced ? C_GRN() : C_AMB()); y += step;
    fmtDur(b, sizeof b, d.uptimeS); rowAt(R0, R1, y, "Uptime", b); y += step;
    snprintf(b, sizeof b, "%lu KB", (unsigned long)d.freeHeapKB); rowAt(R0, R1, y, "Heap", b); y += step;
    snprintf(b, sizeof b, "%lu KB", (unsigned long)d.freePsramKB); rowAt(R0, R1, y, "PSRAM", b);
}

static void drawDots(int y) {
    const int gap = 22, x0 = W / 2 - (PAGE_COUNT - 1) * gap / 2;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == (int)s_page) s_cv->fillCircle(x0 + i * gap, y, 5, C_FG);
        else s_cv->fillCircle(x0 + i * gap, y, 3, rgb(0x70, 0x76, 0x7c));
    }
}

// ---- public ----
void pagesBegin() {
    s_cv = boardCanvas();
    s_dirty = true;
}

void pagesGoto(PageId p) {
    if (s_regenNow || p >= PAGE_COUNT || p == s_page) return;
    s_page = p; s_dirty = true;
}
void pagesNext() { pagesGoto((PageId)((s_page + 1) % PAGE_COUNT)); }
void pagesPrev() { pagesGoto((PageId)((s_page + PAGE_COUNT - 1) % PAGE_COUNT)); }
PageId pagesCurrent() { return s_regenNow ? PAGE_SOOT : s_page; }

static volatile bool s_muted = false;
void pagesSetMuted(bool m) { s_muted = m; s_dirty = true; }

void pagesRender(const UiState &ui, const PagesData &d) {
    if (!s_cv) return;
    s_regenNow = ui.hasData && !ui.stale && ui.regen;
    if (s_regenNow) s_page = PAGE_SOOT;
    uint32_t now = millis();
    if (!s_dirty && now - s_lastDraw < 500) return;
    s_lastDraw = now;
    if (s_page == PAGE_SOOT) {
        uiDraw(ui);
    } else {
        s_cv->fillScreen(C_BG);
        switch (s_page) {
            case PAGE_TREND: drawTrend(d); break;
            case PAGE_TRIP: drawTrip(d); break;
            case PAGE_HEALTH: drawHealth(ui, d); break;
            default: drawSystem(d); break;
        }
    }
    if (!s_regenNow) drawDots(H - 14);
    if (s_muted) putB("muted", M, H - 6, aaR14, rgb(0x90, 0x96, 0x9c));
    // hash the framebuffer; present only if changed
    const uint32_t *p = (const uint32_t *)s_cv->getBuffer();
    uint32_t h = 2166136261u;
    for (size_t i = 0, n = (size_t)W * H / 2; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    if (s_dirty || h != s_hash) { boardPresent(); s_hash = h; }
    s_dirty = false;
}
