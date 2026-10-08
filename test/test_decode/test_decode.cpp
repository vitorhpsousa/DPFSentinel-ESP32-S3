// Golden vectors captured from the real ix35 (CYD raw logs, 2026-10-02) run through the same
// ISO-TP reassembly and decoders the firmware uses.
#include <unity.h>
#include <math.h>
#include <string.h>
#include "obd/isotp.h"
#include "obd/pid_registry.h"

static size_t extract(const char *raw, uint8_t *out) { return isotpExtract(raw, "7E8", out, 96); }
static const PidDef &pid(const char *name) {
    for (size_t i = 0; i < PID_TABLE_LEN; i++) if (!strcmp(PID_TABLE[i].name, name)) return PID_TABLE[i];
    TEST_FAIL_MESSAGE("unknown pid"); return PID_TABLE[0];
}

void test_diff_pressure() {
    uint8_t p[96]; size_t n = extract("7E804611B0012", p);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, pid("dpf_diff_pressure_hpa").decode(p, n));   // 0x0012 * 0.5
}

void test_soot_and_intercooler() {
    uint8_t p[96]; size_t n = extract("7E8100C61FF11001E8B\r7E8215500000305FF55", p);
    TEST_ASSERT_EQUAL(12, n);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.96f, pid("dpf_soot_level_g").decode(p, n));   // 5 * 100/255
    TEST_ASSERT_EQUAL_FLOAT(35.0f, pid("intercooler_temp_c").decode(p, n));          // 0x55 - 50
}

void test_mode21_2103_multiframe() {
    uint8_t p[96];
    size_t n = extract("7E8104B6103FFFFFFFF\r7E82100010001000000\r7E82200001000010010\r7E82300000000000100\r"
                       "7E82401000100010001\r7E82500010001000100\r7E82601000100FF0000\r7E8270000000C077B28\r"
                       "7E828000049DC0C07C5\r7E82904000100000000\r7E82A010001FFFFFF55", p);
    TEST_ASSERT_EQUAL(75, n);
    TEST_ASSERT_EQUAL(0x61, p[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid("dpf_regen_active").decode(p, n));   // p[71] != 4
    float egt = pid("egt_before_dpf_c").decode(p, n);
    TEST_ASSERT_TRUE(egt > 50 && egt < 900);
    TEST_ASSERT_TRUE(pid("odometer_mi").decode(p, n) > 1000);
}

void test_short_and_foreign_frames_are_rejected() {
    uint8_t p[96];
    TEST_ASSERT_EQUAL(0, extract("NO DATA", p));
    TEST_ASSERT_EQUAL(0, isotpExtract("7EA804611B0012", "7E8", p, 96));        // other ECU
    TEST_ASSERT_TRUE(isnan(pid("dpf_soot_level_g").decode(p, 3)));              // truncated payload
}

void test_battery_voltage() {
    uint8_t p[] = {0x41, 0x42, 0x36, 0xB0};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, pid("control_module_v").decode(p, 4));   // 0x36B0 = 14000 mV
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_diff_pressure);
    RUN_TEST(test_soot_and_intercooler);
    RUN_TEST(test_mode21_2103_multiframe);
    RUN_TEST(test_short_and_foreign_frames_are_rejected);
    RUN_TEST(test_battery_voltage);
    return UNITY_END();
}
