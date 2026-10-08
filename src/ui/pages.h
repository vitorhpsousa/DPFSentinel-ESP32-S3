#pragma once
// Touch page manager: soot / trend / trip / system pages on the 320x480 panel.
// Call uiBegin() and pagesBegin() after boardInit(). Touch handling lives elsewhere:
// call pagesNext()/pagesPrev()/pagesGoto() from it. Text: anti-aliased DejaVu (aatext.h).
#include <Arduino.h>
#include "soot_ui.h"

struct PagesData {
    float sootHistory[240];        // newest last; NaN = none
    uint16_t sootHistoryStepMin;   // minutes per sample
    float tripMiles, tripSootStart, tripSootPeak;
    uint8_t regenCount;
    uint32_t lastRegenEndMs;       // millis() at end of last regen; 0 = none
    float lastRegenPeakEgt;
    uint16_t lastRegenMinutes;
    float maxSpeed;                // km/h
    bool bleLinked;
    char sdBackend[8];
    uint32_t sdFreeMB;
    uint32_t bootNumber;
    float batteryV;
    char clockText[24];
    bool clockSynced;
    uint32_t uptimeS;
    uint32_t freeHeapKB, freePsramKB;

    // Health page (populated from dpf/health.h each frame; NAN/-1 = unknown).
    int16_t healthRegensToday;         // -1 = clock not synced yet
    int16_t healthRegensThisWeek;      // -1 = clock not synced yet
    float healthAvgRegenIntervalMi;    // NAN = not enough history yet
    uint16_t healthRegensSinceOil;
    float healthOilChangeOdometerMi;   // 0 = no reference set yet
    float healthLastWarmupMin;         // NAN = none recorded this run
    float healthWarmupMedianMin;       // NAN = no samples yet
};

enum PageId { PAGE_SOOT, PAGE_TREND, PAGE_TRIP, PAGE_HEALTH, PAGE_SYSTEM, PAGE_COUNT };

void pagesBegin();
void pagesSetMuted(bool m);   // shows a small 'muted' marker bottom-left on every page
void pagesNext();
void pagesPrev();
void pagesGoto(PageId p);
PageId pagesCurrent();
// While ui.regen is true the soot/regen screen is always shown and page changes are ignored.
void pagesRender(const UiState &ui, const PagesData &d);
