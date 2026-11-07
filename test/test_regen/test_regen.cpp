// Regens that stop at a standstill, and the idle-pressure warning. Same cases as
// android/core/src/test/.../StandstillTest.kt, so the board and the app agree.
#include <unity.h>
#include <math.h>
#include <vector>
#include "report/regen_watch.h"
#include "dpf/dpf_monitor.h"

void setUp() {}
void tearDown() {}

struct Drive {
    RegenWatch w;
    uint32_t t = 0;
    std::vector<int> events;
    void rows(int n, float flag, float speed, float soot = 18.0f) {
        for (int i = 0; i < n; i++, t += 1000) {
            RegenEvent ev = w.update(t, NAN, flag, 600.0f, soot, 250.0f, 30.0f, speed);
            if (ev.type != RegenEvent::NONE && ev.type != RegenEvent::TICK) events.push_back(ev.type);
        }
    }
};

static void expectEvents(const Drive &d, std::vector<int> want) {
    TEST_ASSERT_EQUAL_INT(want.size(), d.events.size());
    for (size_t i = 0; i < want.size(); i++) TEST_ASSERT_EQUAL_INT(want[i], d.events[i]);
}

void test_drive_through_pauses_and_resumes() {
    Drive d;
    d.rows(120, 1, 60);
    d.rows(300, 0, 0);
    TEST_ASSERT_TRUE(d.w.paused());
    d.rows(20, 0, 30);
    d.rows(600, 1, 70);
    d.rows(5, 0, 70, 1.0f);
    expectEvents(d, {RegenEvent::START, RegenEvent::PAUSED, RegenEvent::RESUMED, RegenEvent::END});
}

void test_empty_filter_stop_is_a_finish() {
    Drive d;
    d.rows(60, 1, 50);
    d.rows(5, 0, 0, 1.5f);
    expectEvents(d, {RegenEvent::START, RegenEvent::END});
}

void test_unknown_speed_keeps_old_finish() {
    Drive d;
    d.rows(60, 1, NAN);
    d.rows(5, 0, NAN);
    expectEvents(d, {RegenEvent::START, RegenEvent::END});
}

void test_pause_times_out() {
    Drive d;
    d.rows(60, 1, 50);
    d.rows(21 * 60, 0, 0);
    expectEvents(d, {RegenEvent::START, RegenEvent::PAUSED, RegenEvent::INTERRUPTED});
}

void test_gap_pauses_then_drive_interrupts() {
    RegenWatch w;
    for (uint32_t i = 0; i < 3; i++) w.update(i * 1000, NAN, 1, 500, 18, 250, NAN);
    TEST_ASSERT_EQUAL_INT(RegenEvent::PAUSED, w.update(200000, NAN, 0, 300, 15, 251, NAN, 0).type);
    uint32_t t = 201000;
    RegenEvent ev;
    while (ev.type == RegenEvent::NONE && t < 600000) { ev = w.update(t, NAN, 0, 300, 15, 252, NAN, 50); t += 1000; }
    TEST_ASSERT_EQUAL_INT(RegenEvent::INTERRUPTED, ev.type);
}

static bool idle(IdleBlockageWatch &w, uint32_t from, int secs, float dp, float coolant = 85, bool regen = false) {
    bool last = false;
    for (int i = 0; i < secs; i++) last = w.update(from + i * 1000, 800, 0, dp, coolant, regen);
    return last;
}

void test_idle_pressure_needs_held_reading() {
    IdleBlockageWatch w;
    TEST_ASSERT_FALSE(idle(w, 0, 59, 40));
    TEST_ASSERT_TRUE(idle(w, 59000, 2, 40));
    TEST_ASSERT_FALSE(idle(w, 61000, 1, 25));
}

void test_idle_pressure_quiet_during_and_after_regen() {
    IdleBlockageWatch w;
    TEST_ASSERT_FALSE(idle(w, 0, 300, 45, 85, true));
    TEST_ASSERT_FALSE(idle(w, 300000, 9 * 60, 45));
    TEST_ASSERT_TRUE(idle(w, 300000 + 9 * 60000, 2 * 60, 45));
}

void test_idle_pressure_ignores_cold_engine() {
    IdleBlockageWatch w;
    TEST_ASSERT_FALSE(idle(w, 0, 120, 45, 40));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_drive_through_pauses_and_resumes);
    RUN_TEST(test_empty_filter_stop_is_a_finish);
    RUN_TEST(test_unknown_speed_keeps_old_finish);
    RUN_TEST(test_pause_times_out);
    RUN_TEST(test_gap_pauses_then_drive_interrupts);
    RUN_TEST(test_idle_pressure_needs_held_reading);
    RUN_TEST(test_idle_pressure_quiet_during_and_after_regen);
    RUN_TEST(test_idle_pressure_ignores_cold_engine);
    return UNITY_END();
}
