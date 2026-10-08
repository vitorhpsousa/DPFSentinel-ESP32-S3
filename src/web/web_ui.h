#pragma once
#include <Arduino.h>
#include "../logging/session_store.h"

// Wi-Fi hotspot + tiny web server: live dashboard at "/", JSON at /api/live
// and /api/files, log downloads at /dl?f=<name>. Runs in its own task so a big
// download never stalls adapter polling.
void webUiBegin(SessionStore *store, uint32_t bootNumber);
void webUiUpdate(const float *values, size_t count, bool linkUp);
float webUiValue(const char *columnName);   // latest value by column name (NAN if none)
void webUiSleep();   // switch WiFi off and stop reconnecting (used before light sleep)
