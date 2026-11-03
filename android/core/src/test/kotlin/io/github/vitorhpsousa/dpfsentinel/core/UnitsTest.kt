package io.github.vitorhpsousa.dpfsentinel.core

import java.time.ZoneOffset
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class UnitsTest {
    @Test fun speedFromTheEcuKmh() {
        assertEquals("50 mph", Units.UK.speed(80.5))
        assertEquals("81 km/h", Units.METRIC.speed(80.5))
        assertEquals("50 mph", Units.US.speed(80.5))
    }

    @Test fun distanceFromTheEcuMiles() {
        assertEquals("100 mi", Units.UK.distance(100.0))
        assertEquals("161 km", Units.METRIC.distance(100.0))
        assertEquals("--", Units.num(Double.NaN, 0))
    }

    @Test fun temperatureOnlyUsIsFahrenheit() {
        assertEquals("80 °C", Units.UK.temp(80.0))
        assertEquals("80 °C", Units.METRIC.temp(80.0))
        assertEquals("176 °F", Units.US.temp(80.0))
    }

    @Test fun sootRatePerKm() {
        assertEquals("0.050 g/mi", Units.UK.sootRate(0.05))
        assertEquals("0.031 g/km", Units.METRIC.sootRate(0.05))
    }

    @Test fun unknownSavedNameFallsBackToUk() {
        assertEquals(Units.UK, Units.fromName(null))
        assertEquals(Units.UK, Units.fromName("bogus"))
        assertEquals(Units.US, Units.fromName("US"))
    }

    @Test fun messagesFollowTheChoice() {
        val w = RegenWatch(ZoneOffset.UTC) { Units.METRIC }
        var t = 0L
        var ev = RegenEvent.NONE
        repeat(3) { ev = w.update(t.also { t += 1000 }, Double.NaN, 1.0, 600.0, 18.0, 250.0, Double.NaN) }
        assertTrue("402.3 km since the last regen" in ev.text, ev.text)
        assertTrue("EGT 600 °C" in ev.text, ev.text)
        assertTrue("under 100 mi apart" in HealthAlert.FAST_REGENS.text(Units.UK))
        assertTrue("under 161 km apart" in HealthAlert.FAST_REGENS.text(Units.METRIC))
        assertTrue("warm-up to 176 °F" in HealthAlert.SLOW_WARMUP.text(Units.US))
    }
}
