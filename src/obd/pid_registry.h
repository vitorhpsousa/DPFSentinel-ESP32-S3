#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Every parameter here was reverse-engineered from a Car Scanner session on
// this exact car (2014 ix35 1.7 CRDi, 2026-09-13): its raw ELM327 log aligned
// sample-for-sample with its decoded CSV. `decode` receives the ISO-TP payload starting at the
// service id, so offsets include it (0x41/0x61 at [0], PID/local id at [1]).
// Only PIDs verified on this car are listed. Mode 22 does NOT exist on this ECU (7F 22 11), and
// intake MAP / baro / fuel level are unsupported, so there is no boost column.
struct PidDef {
    const char *name;
    const char *unit;
    const char *header;     // CAN transmit header: 7DF (mode 01), 7E0 (ECM), 7D4 (steering)
    const char *requestHex;
    const char *frames;     // ELM327 expected-frame-count digit appended to the request
    uint8_t prefix[2];      // payload must start with these bytes
    uint8_t prefixLen;
    float (*decode)(const uint8_t *p, size_t n);
    uint16_t periodMs;      // how often this should be refreshed
};

float dec_rpm(const uint8_t *p, size_t n);
float dec_coolant(const uint8_t *p, size_t n);
float dec_speed(const uint8_t *p, size_t n);
float dec_maf(const uint8_t *p, size_t n);
float dec_load(const uint8_t *p, size_t n);
float dec_egr(const uint8_t *p, size_t n);
float dec_iat(const uint8_t *p, size_t n);
float dec_cat_temp(const uint8_t *p, size_t n);
float dec_module_voltage(const uint8_t *p, size_t n);
float dec_dpf_diff_pressure(const uint8_t *p, size_t n);
float dec_soot(const uint8_t *p, size_t n);
float dec_intercooler(const uint8_t *p, size_t n);
float dec_egt_before_dpf(const uint8_t *p, size_t n);
float dec_dist_since_regen(const uint8_t *p, size_t n);
float dec_odometer(const uint8_t *p, size_t n);
float dec_regen_active(const uint8_t *p, size_t n);
float dec_regen_burning(const uint8_t *p, size_t n);
float dec_eps_speed(const uint8_t *p, size_t n);
float dec_eps_steering(const uint8_t *p, size_t n);
float dec_eps_voltage(const uint8_t *p, size_t n);

// clang-format off
static const PidDef PID_TABLE[] = {
    // --- header 7DF, standard mode 01 ---
    {"engine_rpm",         "rpm",  "7DF", "010C", "1", {0x41, 0x0C}, 2, dec_rpm,        200},
    {"vehicle_speed",      "km/h", "7DF", "010D", "1", {0x41, 0x0D}, 2, dec_speed,      500},
    {"engine_load_pct",    "%",    "7DF", "0104", "1", {0x41, 0x04}, 2, dec_load,       250},
    {"maf_flow",           "g/s",  "7DF", "0110", "1", {0x41, 0x10}, 2, dec_maf,        500},
    {"coolant_temp",       "C",    "7DF", "0105", "1", {0x41, 0x05}, 2, dec_coolant,   2000},
    {"control_module_v",   "V",    "7DF", "0142", "1", {0x41, 0x42}, 2, dec_module_voltage, 2000},
    {"egr_duty_pct",       "%",    "7DF", "012C", "1", {0x41, 0x2C}, 2, dec_egr,       2000},
    {"intake_air_temp_c",  "C",    "7DF", "010F", "1", {0x41, 0x0F}, 2, dec_iat,       5000},
    // 013E is Catalyst Temp Bank 1 Sensor 2; column name kept for continuity
    // with the Pi logs. The actual DPF-inlet temperature is egt_before_dpf_c.
    {"dpf_zone_temp_c",    "C",    "7DF", "013E", "1", {0x41, 0x3E}, 2, dec_cat_temp,  1000},

    // --- header 7E0, ECM manufacturer mode 21 ---
    {"dpf_diff_pressure_hpa","hPa","7E0", "211B", "1", {0x61, 0x1B}, 2, dec_dpf_diff_pressure, 1000},
    {"egt_before_dpf_c",   "C",    "7E0", "2103", "B", {0x61, 0x03}, 2, dec_egt_before_dpf,    1000},
    {"dpf_dist_since_regen_mi","mi","7E0","2103", "B", {0x61, 0x03}, 2, dec_dist_since_regen,  1000},
    {"odometer_mi",        "mi",   "7E0", "2103", "B", {0x61, 0x03}, 2, dec_odometer,          1000},
    {"dpf_regen_active",   "bool", "7E0", "2103", "B", {0x61, 0x03}, 2, dec_regen_active,      1000},
    {"dpf_regen_burning",  "bool", "7E0", "2103", "B", {0x61, 0x03}, 2, dec_regen_burning,     1000},
    {"dpf_soot_level_g",   "g",    "7E0", "21948001","2", {0x61, 0x00}, 1, dec_soot,           2000},
    {"intercooler_temp_c", "C",    "7E0", "21948001","2", {0x61, 0x00}, 1, dec_intercooler,    2000},

    // --- header 7D4, steering ECU ---
    {"eps_speed_kmh",      "km/h", "7D4", "2101", "3", {0x61, 0x01}, 2, dec_eps_speed,   5000},
    {"eps_steering_deg",   "deg",  "7D4", "2101", "3", {0x61, 0x01}, 2, dec_eps_steering,5000},
    {"eps_voltage_v",      "V",    "7D4", "2101", "3", {0x61, 0x01}, 2, dec_eps_voltage, 5000},
};
// clang-format on
static const size_t PID_TABLE_LEN = sizeof(PID_TABLE) / sizeof(PID_TABLE[0]);

// Columns computed in main.cpp from the above (never requested from the ECU).
static const char *const DERIVED_NAMES[] = {"dpf_odo_at_last_regen_mi"};
static const size_t DERIVED_LEN = sizeof(DERIVED_NAMES) / sizeof(DERIVED_NAMES[0]);
static const size_t COLUMN_COUNT = PID_TABLE_LEN + DERIVED_LEN;

static inline const char *columnName(size_t i) {
    return i < PID_TABLE_LEN ? PID_TABLE[i].name : DERIVED_NAMES[i - PID_TABLE_LEN];
}

static inline const char *rxIdFor(const char *header) {
    if (header[0] == '7' && header[1] == 'D' && header[2] == '4') return "7DC";
    return "7E8";
}
