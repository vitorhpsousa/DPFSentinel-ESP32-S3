package io.github.vitorhpsousa.dpfsentinel.core

import java.time.ZoneOffset
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LogicTest {
    private val nan = Double.NaN

    // A fake ELM327 that answers like the ix35 did.
    private class FakeElm(val rejectCount: Boolean = false) : ElmTransport {
        val sent = mutableListOf<String>()
        var header = ""
        var redundantAtsh = 0
        override fun send(cmd: String, timeoutMs: Long): String {
            sent.add(cmd)
            if (cmd.startsWith("ATSH")) { if (cmd.substring(4) == header) redundantAtsh++; header = cmd.substring(4); return "OK" }
            if (cmd.startsWith("AT")) return if (cmd == "ATZ") "ELM327 v1.5" else "OK"
            if (rejectCount && cmd.length % 2 == 1) return "?"
            // Requests are even-length hex; an odd length carries the frame-count digit.
            return when (if (cmd.length % 2 == 1) cmd.dropLast(1) else cmd) {
                "211B" -> "7E804611B0012"
                "21948001" -> "7E8100C61FF11001E8B\r7E8215500000305FF55"
                "010C" -> "7E804410C1BC4"
                else -> "NO DATA"
            }
        }
    }

    @Test fun pollerDecodesAndSharesHeaders() {
        val elm = FakeElm()
        val p = ObdPoller(elm)
        assertTrue(p.initAdapter(0))
        repeat(20) { p.step(0, false) }
        val row = p.assembleRow(0)
        assertEquals(9.0, row.diffPressure)
        assertEquals(1.96, row.soot, 0.01)
        assertEquals(1777.0, row.rpm)   // 0x1BC4 / 4
        assertTrue(row.egt.isNaN())
        // ATSH is only sent when the header changes.
        assertEquals(0, elm.redundantAtsh)
    }

    @Test fun pollerDropsRejectedFrameCount() {
        val elm = FakeElm(rejectCount = true)
        val p = ObdPoller(elm)
        p.initAdapter(0)
        repeat(20) { p.step(0, false) }
        assertEquals(9.0, p.assembleRow(0).diffPressure)
        assertTrue("211B" in elm.sent)
    }

    @Test fun staleValuesExpire() {
        val p = ObdPoller(FakeElm())
        p.initAdapter(0)
        repeat(20) { p.step(0, false) }
        assertTrue(p.assembleRow(10_000).diffPressure.isNaN())
    }

    @Test fun regenStartFinishDebounced() {
        val w = RegenWatch(ZoneOffset.UTC)
        var t = 0L
        fun step(active: Double) = w.update(t.also { t += 1000 }, 1.8e9, active, 600.0, 18.0, 250.0, 30.0)
        assertEquals(RegenEvent.Type.NONE, step(1.0).type)
        assertEquals(RegenEvent.Type.NONE, step(1.0).type)
        val start = step(1.0)
        assertEquals(RegenEvent.Type.START, start.type)
        assertTrue(start.text.startsWith("DPF regeneration started\nsoot 18.0 g, 250.0 mi"))
        assertTrue("DPF pressure 30 hPa" in start.text)
        step(0.0); step(0.0)
        assertEquals(RegenEvent.Type.END, step(0.0).type)
    }

    @Test fun regenInterruptedAfterGap() {
        val w = RegenWatch(ZoneOffset.UTC)
        repeat(3) { w.update(it * 1000L, nan, 1.0, 500.0, 18.0, 250.0, nan) }
        val ev = w.update(200_000, nan, 1.0, 500.0, 15.0, 251.0, nan)
        assertEquals(RegenEvent.Type.INTERRUPTED, ev.type)
    }

    private fun row(soot: Double, dist: Double, odo: Double = 50_000.0 + dist, coolant: Double = 90.0): Row = Row().also {
        it.values[Pids.col("dpf_soot_level_g")] = soot
        it.values[Pids.col("dpf_dist_since_regen_mi")] = dist
        it.values[Pids.col("odometer_mi")] = odo
        it.values[Pids.col("coolant_temp")] = coolant
    }

    @Test fun estimateFollowsLiveRateAndNotSoon() {
        val h = HealthTracker(zone = ZoneOffset.UTC)
        // A completed regen: distance drops to ~0 with an empty filter, two rows running.
        h.update(row(17.0, 300.0), RegenEvent.NONE, 0, nan)
        h.update(row(1.0, 0.2), RegenEvent.NONE, 1000, nan)
        h.update(row(1.0, 0.3), RegenEvent.NONE, 2000, nan)
        assertEquals(0.3, h.state.baseDistMi, 1e-9)
        assertEquals(1.0, h.state.baseSootG, 1e-9)
        // 200 mi on from the baseline: soot 11 g is 0.05 g/mi -> "not soon"; at 15 g it is 0.07 g/mi -> ~38 mi
        h.update(row(11.0, 200.3), RegenEvent.NONE, 3000, nan)
        assertTrue(h.state.estNotSoon)
        assertEquals(0.05, h.state.estRateGPerMi, 1e-6)
        h.update(row(15.0, 200.3), RegenEvent.NONE, 4000, nan)
        assertFalse(h.state.estNotSoon)
        assertTrue(h.state.estFromLiveRate)
        assertEquals((17.65 - 15.0) / 0.07, h.state.estMilesToRegen, 1e-6)
    }

    @Test fun regenStartTeachesTriggerAndPersists() {
        val h = HealthTracker(zone = ZoneOffset.UTC)
        h.update(row(18.5, 280.0), RegenEvent(RegenEvent.Type.START, "x"), 0, 1.8e9)
        assertEquals(1, h.state.regenHistory.size)
        assertEquals(18.5, h.regenTriggerG())
        assertTrue(h.dirty)
        val back = HealthState.decode(h.state.encode())
        assertEquals(1, back.regenHistory.size)
        assertEquals(280.0, back.regenHistory[0].milesSincePrev)
        assertEquals(listOf(18.5), back.triggerHist)
        assertEquals(1, back.regensSinceOil)
        assertTrue(back.lastWarmupMinutes.isNaN())
    }

    @Test fun fastRegenAlertOncePerEpisode() {
        val h = HealthTracker(zone = ZoneOffset.UTC)
        for (i in 0 until 3) h.update(row(18.0, 80.0), RegenEvent(RegenEvent.Type.START, ""), i * 1000L, nan)
        assertEquals(listOf(HealthAlert.FAST_REGENS), h.takeAlerts())
        assertEquals(emptyList(), h.takeAlerts())
    }

    @Test fun corruptHealthBlobStartsClean() {
        assertEquals(0, HealthState.decode("ver=1\nregens=garbage").regenHistory.size)
        assertEquals(0, HealthState.decode("ver=0\nsinceOil=5").regensSinceOil)
    }

    @Test fun sootZoneHysteresis() {
        val z = SootZone()
        assertEquals(null, z.update(10.0))
        assertEquals(1, z.update(14.2))
        assertEquals(null, z.update(13.8))   // inside the hysteresis band
        assertEquals(2, z.update(17.1))
        assertEquals(null, z.update(16.7))
        assertEquals(null, z.update(16.4))   // drops to amber, no alert
        assertEquals(1, z.zone)
        assertEquals(2, z.update(17.0))
    }
}
