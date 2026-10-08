#include "aatext.h"

static uint32_t nextCp(const char *&p) {
    uint8_t c = (uint8_t)*p++;
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = extra == 3 ? (c & 7) : extra == 2 ? (c & 15) : extra == 1 ? (c & 31) : 0xFFFD;
    while (extra-- > 0 && ((uint8_t)*p & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)*p++ & 63);
    return cp;
}

static const AAGlyph *findGlyph(const AAFont &f, uint32_t cp) {
    int lo = 0, hi = (int)f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f.cps[mid] == cp) return &f.glyphs[mid];
        if (f.cps[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

static int advanceOf(const AAFont &f, uint32_t cp) {
    const AAGlyph *g = findGlyph(f, cp);
    if (g) return g->adv;
    g = findGlyph(f, ' ');
    return g ? g->adv : f.px / 3;
}

int aaMeasure(const AAFont &f, const char *s) {
    int w = 0;
    while (*s) w += advanceOf(f, nextCp(s));
    return w;
}

// RGB565 blend, alpha 0..255 (bit trick: spread G into the upper half so R,G,B blend in one multiply).
static inline uint16_t blend(uint16_t dst, uint32_t srcSpread, uint16_t src, uint8_t a) {
    if (a == 255) return src;
    uint32_t a5 = (a + (a >> 7)) >> 3;                 // 0..32
    uint32_t d = (dst | ((uint32_t)dst << 16)) & 0x07E0F81Fu;
    uint32_t r = (d + ((((srcSpread - d) & 0xFFFFFFFFu) * a5) >> 5)) & 0x07E0F81Fu;
    return (uint16_t)(r | (r >> 16));
}

int aaDraw(GFXcanvas16 *cv, int x, int y, const char *s, const AAFont &f, uint16_t col) {
    const int cw = cv->width(), ch = cv->height();
    uint16_t *buf = cv->getBuffer();
    const uint32_t spread = (col | ((uint32_t)col << 16)) & 0x07E0F81Fu;
    int pen = x;
    while (*s) {
        uint32_t cp = nextCp(s);
        const AAGlyph *g = findGlyph(f, cp);
        if (!g) { pen += advanceOf(f, cp); continue; }
        const uint8_t *bm = f.bitmap + g->off;
        int gx = pen + g->bx, gy = y + g->by;
        for (int r = 0; r < g->h; r++) {
            int py = gy + r;
            const uint8_t *row = bm + r * g->w;
            if (py < 0 || py >= ch) continue;
            uint16_t *dp = buf + (size_t)py * cw;
            for (int c = 0; c < g->w; c++) {
                uint8_t a = row[c];
                int px = gx + c;
                if (!a || px < 0 || px >= cw) continue;
                dp[px] = blend(dp[px], spread, col, a);
            }
        }
        pen += g->adv;
    }
    return pen - x;
}

int aaDrawCentered(GFXcanvas16 *cv, int cx, int y, const char *s, const AAFont &f, uint16_t col) {
    return aaDraw(cv, cx - aaMeasure(f, s) / 2, y, s, f, col);
}
int aaDrawRight(GFXcanvas16 *cv, int xr, int y, const char *s, const AAFont &f, uint16_t col) {
    return aaDraw(cv, xr - aaMeasure(f, s), y, s, f, col);
}

const AAFont &aaFit(const char *s, int maxW, const AAFont *const *cands, int n) {
    for (int i = 0; i < n; i++) if (aaMeasure(*cands[i], s) <= maxW) return *cands[i];
    return *cands[n - 1];
}
