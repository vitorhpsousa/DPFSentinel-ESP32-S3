#pragma once
#include <math.h>

// The ECU's own odometer reading (odometer_mi, logged raw in every CSV) can
// differ from the dash by anywhere from a few miles to a few hundred, because
// several makes derive the dash figure from the ABS/wheel-speed sensors
// (tyre-size dependent) while the ECU counts its own, separate pulse source.
// Neither is "wrong"; they're just not the same counter. This is a constant
// offset you set once by comparing the two, applied ONLY where a reading is
// shown to a person (the screen, the web live view) — never to what's
// written to the CSV, so logged data always matches what the ECU itself
// reported and stays comparable across cars/sessions.
//
// Set it from the board's web page: visit (with the engine on, so there's a
// live ECU reading to compare against) http://<board-ip>/api/odometer?dash=123456
// with 123456 replaced by your dash's current mileage. See docs/odometer-offset.md.
//
// Persisted via Preferences (NVS), namespace "health" (same as dpf/health.*) key "odoOff".

void odometerOffsetBegin();   // load the saved offset; call once at boot, after healthBegin()
float odometerOffsetMi();     // the current offset (0 if never set)

// Call with the dash's displayed mileage and the ECU's live odometer_mi reading
// (NAN if there isn't one yet, e.g. engine off/adapter not connected) to set and
// persist the offset. Returns false (and leaves the offset unchanged) if
// ecuOdometerMi is NaN — there's nothing to compare the dash figure against yet.
bool odometerOffsetSet(float dashReadingMi, float ecuOdometerMi);

// ecuOdometerMi + the stored offset, for display. Passes NaN straight through.
static inline float odometerCorrected(float ecuOdometerMi) {
    return isnan(ecuOdometerMi) ? ecuOdometerMi : ecuOdometerMi + odometerOffsetMi();
}
