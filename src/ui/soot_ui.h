#pragma once
// Soot panel UI for the 320x480 portrait ST77922 (port of pi/screen/soot_panel.py).
// Draws into the board's GFXcanvas16; pages.cpp presents it. Call boardInit() first.
#include <Arduino.h>

struct UiState {
    float soot, rpm, speed, coolant, diffP, catTemp, intercooler, maf, sinceRegen, odometer;  // NaN = missing
    bool regen;
    bool hasData;  // false -> "NO LOGGER DATA"
    bool stale;    // true (with hasData) -> "NO LIVE DATA"
    char ip[20];
    char ssid[33];
    char clock[24];   // "Sat 26 Sep 18:37" or "clock not set"
};

void uiBegin();
void uiDraw(const UiState &s);        // paints the whole soot screen into the canvas (no present)
