#pragma once
// Anti-aliased text (DejaVu Sans, pre-rendered by tools/gen_fonts.py) blended onto a GFXcanvas16.
// Strings are UTF-8 (the degree sign is "\xC2\xB0"). y is the top of the font's line box (font.height tall).
#include <Adafruit_GFX.h>
#include "fonts_aa.h"

int aaMeasure(const AAFont &f, const char *s);                                   // advance width in px
int aaDraw(GFXcanvas16 *cv, int x, int y, const char *s, const AAFont &f, uint16_t col);   // returns width
int aaDrawCentered(GFXcanvas16 *cv, int cx, int y, const char *s, const AAFont &f, uint16_t col);
int aaDrawRight(GFXcanvas16 *cv, int xr, int y, const char *s, const AAFont &f, uint16_t col);
// First candidate (list them largest to smallest) whose width fits maxW, else the last one.
const AAFont &aaFit(const char *s, int maxW, const AAFont *const *cands, int n);
