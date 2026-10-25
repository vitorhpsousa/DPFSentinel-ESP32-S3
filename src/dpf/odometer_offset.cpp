#include "odometer_offset.h"
#include <Preferences.h>

static Preferences g_prefs;
static float g_offset = 0.0f;

void odometerOffsetBegin() {
    g_prefs.begin("health", true);  // read-only open: same namespace dpf/health.cpp uses
    g_offset = g_prefs.isKey("odoOff") ? g_prefs.getFloat("odoOff", 0.0f) : 0.0f;
    g_prefs.end();
}

float odometerOffsetMi() { return g_offset; }

bool odometerOffsetSet(float dashReadingMi, float ecuOdometerMi) {
    if (isnan(ecuOdometerMi)) return false;
    g_offset = dashReadingMi - ecuOdometerMi;
    g_prefs.begin("health", false);
    g_prefs.putFloat("odoOff", g_offset);
    g_prefs.end();
    return true;
}
