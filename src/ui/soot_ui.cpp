#include "soot_ui.h"
#include <math.h>
#include <string.h>
#include "../board/board.h"
#include "aatext.h"

static const int W = UI_W, H = UI_H, MARGIN = 8;
static const float GREEN_MAX = 14.0f, AMBER_MAX = 17.0f, DARK_AT = 28.0f;

static GFXcanvas16 *cv = nullptr;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

static uint16_t sootColor(float g) {
    if (isnan(g)) return rgb(0x3a, 0x3f, 0x44);
    if (g < GREEN_MAX) return rgb(0x1f, 0xa5, 0x4c);
    if (g < AMBER_MAX) return rgb(0xe8, 0xa5, 0x48);
    float t = fminf(1.0f, (g - AMBER_MAX) / (DARK_AT - AMBER_MAX));
    return rgb(0xd0 + (0x8b - 0xd0) * t, 0x2c + (0x00 - 0x2c) * t, 0x20 + (0x00 - 0x20) * t);
}

static void fmt(char *out, size_t n, float v, int dec, const char *unit) {
    if (isnan(v)) strlcpy(out, "-", n);
    else snprintf(out, n, "%.*f%s", dec, v, unit);
}

// Anti-aliased DejaVu text (aatext). Fit lists are largest -> smallest.
static const AAFont *const F_LBL[] = {&aaR17, &aaR14};
static const AAFont *const F_VAL[] = {&aaR24, &aaR20, &aaR17};
static const AAFont *const F_BIG[] = {&aaB30, &aaB24, &aaB20};
static const AAFont *const F_MSG[] = {&aaB44, &aaB30, &aaB24};
static const AAFont *const F_NET[] = {&aaR20, &aaR17, &aaR14};
static const AAFont *const F_HDR[] = {&aaB24, &aaB20, &aaB17};
static const AAFont *const F_RG1[] = {&aaB30, &aaB24, &aaB20, &aaB17};
static const AAFont *const F_HUGE[] = {&aaB90, &aaB44, &aaB30};
template <size_t N> static const AAFont &fit(const char *t, int maxW, const AAFont *const (&l)[N]) { return aaFit(t, maxW, l, N); }

// Text with its baseline at yb, so different fitted sizes on one row stay aligned.
static void leftB(const char *t, int x, int yb, const AAFont &f, uint16_t col) { aaDraw(cv, x, yb - f.ascent, t, f, col); }
static int centeredT(const char *t, int cx, int y, const AAFont &f, uint16_t col) { aaDrawCentered(cv, cx, y, t, f, col); return y + f.height; }

// IP + SSID on one centred line (like the Pi's net_text()).
static void drawNet(const UiState &s, int y, const AAFont *const *l, int n, uint16_t col) {
    char buf[64];
    const char *ip = s.ip[0] ? s.ip : "no network";
    if (s.ssid[0]) snprintf(buf, sizeof buf, "%s %s", ip, s.ssid);
    else strlcpy(buf, ip, sizeof buf);
    aaDrawCentered(cv, W / 2, y, buf, aaFit(buf, W - 2 * MARGIN, l, n), col);
}

void uiBegin() {
    cv = boardCanvas();
}

void uiDraw(const UiState &s) {
    if (!cv) return;
    const int cx = W / 2;
    uint16_t fg = 0xFFFF, lbl = rgb(0xcc, 0xcc, 0xcc), bg;
    const bool noData = !s.hasData || s.stale;
    if (noData) bg = rgb(0x3a, 0x3f, 0x44);
    else if (s.regen) bg = rgb(0x1c, 0x1c, 0x1c);
    else {
        bg = sootColor(s.soot);
        if (bg == rgb(0xe8, 0xa5, 0x48)) { fg = 0x0000; lbl = rgb(0x33, 0x33, 0x33); }   // amber: dark text
    }
    cv->fillScreen(bg);

    char soot[12], v[10][16];
    fmt(soot, sizeof soot, s.soot, 1, "g");
    fmt(v[0], 16, s.rpm, 0, "");
    fmt(v[1], 16, s.speed, 0, " km/h");
    fmt(v[2], 16, s.coolant, 0, "\xC2\xB0" "C");
    fmt(v[3], 16, s.diffP, 1, " hPa");
    fmt(v[4], 16, s.catTemp, 0, "\xC2\xB0" "C");
    fmt(v[5], 16, s.intercooler, 0, "\xC2\xB0" "C");
    fmt(v[6], 16, s.maf, 1, " g/s");
    fmt(v[7], 16, s.sinceRegen, 1, " mi");
    strlcpy(v[8], s.regen ? "ACTIVE" : "OFF", 16);
    fmt(v[9], 16, s.odometer, 0, " mi");

    if (noData) {
        const char *msg = !s.hasData ? "NO LOGGER DATA" : "NO LIVE DATA";
        centeredT(msg, cx, 100, fit(msg, W - 2 * MARGIN, F_MSG), fg);
        drawNet(s, 200, F_NET, 3, fg);
        centeredT(s.clock, cx, 250, aaR24, fg);
        return;
    }
    if (s.regen) {
        // Regen screen: the warning stays on top; below it a 3 x 3 grid of the live driving data.
        const uint16_t amber = rgb(0xff, 0xc0, 0x40);
        const int mw = W - 2 * MARGIN;
        centeredT("ACTIVE REGENERATION", cx, 6, fit("ACTIVE REGENERATION", mw, F_HDR), fg);
        const char *warn = "DO NOT SWITCH OFF UNTIL IT FINISHES";
        centeredT(warn, cx, 38, fit(warn, mw, F_RG1), amber);
        static const char *rl[9] = {"Soot", "RPM", "Speed", "Cat. Temp", "Coolant", "Diff P.", "Since Regen", "Intercooler", "MAF"};
        const char *rv[9] = {soot, v[0], v[1], v[4], v[2], v[3], v[7], v[5], v[6]};
        const int cw = W / 3, cellW2 = cw - MARGIN - 2;
        const int gtop = 74, rowH2 = 68;
        for (int n = 0; n < 9; n++) {
            int x = (n % 3) * cw + MARGIN;
            int ry = gtop + (n / 3) * rowH2;
            leftB(rl[n], x, ry + 15, aaR17, lbl);
            leftB(rv[n], x, ry + 46, fit(rv[n], cellW2, F_BIG), n == 0 ? amber : fg);   // soot in amber: the number to watch
        }
        drawNet(s, H - MARGIN - aaR14.height, F_NET + 2, 1, lbl);
        return;
    }
    // Normal detail grid, 2 columns x 6 rows like the Pi: Soot first (top-left, a size up), then
    // RPM, Speed, Coolant, Diff P., Cat. Temp, Intercooler, MAF, Since Regen, Regen, Odometer, IP+SSID.
    // Bottom 20 px: page dots (pages.cpp, centre) and the clock (bottom right).
    static const char *labels[11] = {"RPM", "Speed", "Coolant", "Diff P.", "Cat. Temp",
                                     "Intercooler", "MAF", "Since Regen", "Regen", "Odometer", "IP"};
    const int colW = W / 2, cellW = colW - MARGIN - 4;
    const int top = MARGIN, bottom = H - 28;
    const int sootH = 56;
    const int rowH = (bottom - top - sootH) / 5;
    // row 0: Soot | RPM  (label ink top ~ +3, value baseline +46)
    leftB("Soot", MARGIN + 2, top + 15, aaR17, lbl);
    leftB(soot, MARGIN + 2, top + 46, fit(soot, cellW, F_BIG), fg);
    leftB(labels[0], colW + MARGIN, top + 15, aaR17, lbl);
    leftB(v[0], colW + MARGIN, top + 46, fit(v[0], cellW, F_BIG), fg);
    // cells 2..11 (Speed .. IP+SSID); cell n has column n%2, row n/2
    char ipl[64];
    if (s.ssid[0]) snprintf(ipl, sizeof ipl, "IP  %s", s.ssid); else strlcpy(ipl, "IP", sizeof ipl);
    for (int n = 2; n <= 11; n++) {
        int x = (n % 2) * colW + MARGIN;
        int ry = top + sootH + (n / 2 - 1) * rowH;
        const char *lab = labels[n - 1], *val;
        if (n == 11) { lab = ipl; val = s.ip[0] ? s.ip : "no network"; }
        else val = v[n - 1];
        leftB(lab, x, ry + 15, fit(lab, cellW, F_LBL), lbl);
        leftB(val, x, ry + 40, fit(val, cellW, F_VAL), fg);
    }
    aaDrawRight(cv, W - MARGIN, H - 22, s.clock, aaR17, lbl);
}
