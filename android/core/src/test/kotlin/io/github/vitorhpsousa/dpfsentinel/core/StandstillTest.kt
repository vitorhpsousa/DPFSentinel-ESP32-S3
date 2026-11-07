package io.github.vitorhpsousa.dpfsentinel.core

import java.time.ZoneOffset
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

// Regens that stop at a standstill, and the idle-pressure warning during/after them.
class StandstillTest {
    private val nan = Double.NaN

    private class Drive(val w: RegenWatch = RegenWatch(ZoneOffset.UTC)) {
        var t = 0L
        val events = mutableListOf<RegenEvent.Type>()
        fun rows(n: Int, flag: Double, speed: Double, soot: Double = 18.0) = repeat(n) {
            val ev = w.update(t, Double.NaN, flag, 600.0, soot, 250.0, 30.0, speed)
            if (ev.type != RegenEvent.Type.NONE && ev.type != RegenEvent.Type.TICK) events += ev.type
            t += 1000
        }
    }

    @Test fun driveThroughPausesAndResumes() {
        val d = Drive()
        d.rows(120, 1.0, 60.0)          // regen while driving
        d.rows(300, 0.0, 0.0)           // 5 min in the drive-through queue, ECU holds the regen
        assertTrue(d.w.paused)
        d.rows(20, 0.0, 30.0)           // pulls away, flag comes back after 20 s
        d.rows(600, 1.0, 70.0)
        d.rows(5, 0.0, 70.0, soot = 1.0)
        assertEquals(listOf(RegenEvent.Type.START, RegenEvent.Type.PAUSED, RegenEvent.Type.RESUMED, RegenEvent.Type.END), d.events)
    }

    @Test fun noStillRunningPingsWhilePaused() {
        val w = RegenWatch(ZoneOffset.UTC)
        var t = 0L
        repeat(10) { w.update(t, nan, 1.0, 600.0, 18.0, 250.0, nan, 50.0); t += 1000 }
        repeat(5) { w.update(t, nan, 0.0, 600.0, 18.0, 250.0, nan, 0.0); t += 1000 }
        repeat(120) {
            val ev = w.update(t, nan, 0.0, 600.0, 18.0, 250.0, nan, 0.0)
            assertFalse(ev.type == RegenEvent.Type.TICK)
            t += 1000
        }
    }

    @Test fun flagDropWhileStoppedWithEmptyFilterIsAFinish() {
        val d = Drive()
        d.rows(60, 1.0, 50.0)
        d.rows(5, 0.0, 0.0, soot = 1.5)
        assertEquals(listOf(RegenEvent.Type.START, RegenEvent.Type.END), d.events)
    }

    @Test fun flagDropWhileDrivingIsStillAFinish() {
        val d = Drive()
        d.rows(60, 1.0, 50.0)
        d.rows(5, 0.0, 50.0, soot = 9.0)
        assertEquals(listOf(RegenEvent.Type.START, RegenEvent.Type.END), d.events)
    }

    @Test fun pauseThatNeverResumesTimesOut() {
        val d = Drive()
        d.rows(60, 1.0, 50.0)
        d.rows(21 * 60, 0.0, 0.0)       // parked with the engine idling for 21 min
        assertEquals(listOf(RegenEvent.Type.START, RegenEvent.Type.PAUSED, RegenEvent.Type.INTERRUPTED), d.events)
    }

    // ---- idle pressure ----

    private fun idle(w: IdleBlockageWatch, fromMs: Long, secs: Int, dp: Double, coolant: Double = 85.0, regen: Boolean = false): Boolean {
        var last = false
        for (i in 0 until secs) last = w.update(fromMs + i * 1000L, 800.0, 0.0, dp, coolant, regen)
        return last
    }

    @Test fun idlePressureNeedsAHeldHighReading() {
        val w = IdleBlockageWatch()
        assertFalse(idle(w, 0, 59, 40.0))
        assertTrue(idle(w, 59_000, 2, 40.0))
        assertFalse(idle(w, 61_000, 1, 25.0))   // a normal loaded filter at idle
    }

    @Test fun idlePressureQuietDuringAndJustAfterRegen() {
        val w = IdleBlockageWatch()
        assertFalse(idle(w, 0, 300, 45.0, regen = true))            // traffic jam mid-regen
        assertFalse(idle(w, 300_000, 9 * 60, 45.0))                 // the 10 min after it
        assertTrue(idle(w, 300_000 + 9 * 60_000L, 2 * 60, 45.0))     // later on, it counts
    }

    @Test fun idlePressureIgnoresColdEngineAndMoving() {
        val w = IdleBlockageWatch()
        assertFalse(idle(w, 0, 120, 45.0, coolant = 40.0))
        var flagged = false
        for (i in 0 until 120) flagged = w.update(i * 1000L, 1000.0, 30.0, 45.0, 85.0, false)
        assertFalse(flagged)
    }
}
