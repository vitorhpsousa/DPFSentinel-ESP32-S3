package io.github.vitorhpsousa.dpfsentinel.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class MotionTest {
    @Test
    fun phoneHeaderExtendsFirmwareHeader() {
        assertTrue(PhoneColumns.HEADER.startsWith(CsvFormat.HEADER + ","))
        assertEquals(PhoneColumns.NAMES.size, PhoneColumns.empty().size)
    }

    @Test
    fun pressureAltitude() {
        assertEquals(0.0, GradeTracker.pressureAltitudeM(1013.25), 0.01)
        // ~8.4 m per hPa near sea level
        assertEquals(8.4, GradeTracker.pressureAltitudeM(1012.25), 0.2)
    }

    @Test
    fun steadyClimbGivesItsGrade() {
        val g = GradeTracker()
        // 54 km/h = 15 m/s, climbing 0.9 m/s = 6 %, GPS noisy around the true height + 40 m
        for (s in 0..60) {
            val alt = 100.0 + 0.9 * s
            val noise = if (s % 2 == 0) 8.0 else -8.0
            g.update(s * 1000L, 54.0, alt, alt + 40 + noise, 5.0)
        }
        assertEquals(6.0, g.gradePct, 0.6)
        assertEquals(15.0 * 60, g.distanceM, 1.0)
    }

    @Test
    fun noGradeWhenStoppedOrNoAltitude() {
        val g = GradeTracker()
        for (s in 0..30) g.update(s * 1000L, 0.0, 100.0 + s, Double.NaN)
        assertTrue(g.gradePct.isNaN())
        val h = GradeTracker()
        for (s in 0..30) h.update(s * 1000L, 50.0, Double.NaN, Double.NaN)
        assertTrue(h.gradePct.isNaN() && h.altitudeM.isNaN())
    }

    @Test
    fun gpsOnlyStillGivesAltitude() {
        val g = GradeTracker()
        for (s in 0..30) g.update(s * 1000L, 50.0, Double.NaN, 200.0, 4.0)
        assertEquals(200.0, g.altitudeM, 0.5)
        assertEquals(0.0, g.gradePct, 0.5)
    }
}
