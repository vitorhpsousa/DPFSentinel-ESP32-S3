#include "pid_registry.h"

static inline float u8(const uint8_t *p, size_t i) { return (float)p[i]; }
static inline uint32_t u16(const uint8_t *p, size_t i) { return ((uint32_t)p[i] << 8) | p[i + 1]; }
static inline uint32_t u24(const uint8_t *p, size_t i) { return ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2]; }
static inline uint32_t u32(const uint8_t *p, size_t i) { return ((uint32_t)p[i] << 24) | u24(p, i + 1); }

static const float MI_PER_M = 0.000621371f;  // metres -> miles

float dec_rpm(const uint8_t *p, size_t n)        { return n < 4 ? NAN : u16(p, 2) * 0.25f; }
float dec_coolant(const uint8_t *p, size_t n)    { return n < 3 ? NAN : u8(p, 2) - 40.0f; }
float dec_speed(const uint8_t *p, size_t n)      { return n < 3 ? NAN : u8(p, 2); }
float dec_maf(const uint8_t *p, size_t n)        { return n < 4 ? NAN : u16(p, 2) * 0.01f; }
float dec_load(const uint8_t *p, size_t n)       { return n < 3 ? NAN : u8(p, 2) * 100.0f / 255.0f; }
float dec_egr(const uint8_t *p, size_t n)        { return n < 3 ? NAN : u8(p, 2) * 100.0f / 255.0f; }
float dec_iat(const uint8_t *p, size_t n)        { return n < 3 ? NAN : u8(p, 2) - 40.0f; }
float dec_cat_temp(const uint8_t *p, size_t n)   { return n < 4 ? NAN : u16(p, 2) * 0.1f - 40.0f; }

float dec_dpf_diff_pressure(const uint8_t *p, size_t n) { return n < 4 ? NAN : u16(p, 2) * 0.5f; }
float dec_intercooler(const uint8_t *p, size_t n)       { return n < 12 ? NAN : u8(p, 6) - 50.0f; }
float dec_soot(const uint8_t *p, size_t n)              { return n < 12 ? NAN : u8(p, 10) * 100.0f / 255.0f; }
float dec_egt_before_dpf(const uint8_t *p, size_t n)    { return n < 75 ? NAN : u16(p, 48) * 0.0183966f + 99.013f; }
float dec_dist_since_regen(const uint8_t *p, size_t n)  { return n < 75 ? NAN : u24(p, 56) * MI_PER_M; }
float dec_odometer(const uint8_t *p, size_t n)          { return n < 75 ? NAN : u32(p, 59) * MI_PER_M; }
float dec_regen_active(const uint8_t *p, size_t n)      { return n < 75 ? NAN : (p[71] == 4 ? 1.0f : 0.0f); }
float dec_regen_burning(const uint8_t *p, size_t n)     { return n < 75 ? NAN : (p[19] == 21 ? 1.0f : 0.0f); }

float dec_eps_speed(const uint8_t *p, size_t n)    { return n < 13 ? NAN : u8(p, 5); }
float dec_eps_steering(const uint8_t *p, size_t n) { return n < 13 ? NAN : (int16_t)u16(p, 11) * 0.1f; }
float dec_eps_voltage(const uint8_t *p, size_t n)  { return n < 13 ? NAN : u8(p, 3) * 0.1f; }

float dec_module_voltage(const uint8_t *p, size_t n) { return n < 4 ? NAN : u16(p, 2) * 0.001f; }
