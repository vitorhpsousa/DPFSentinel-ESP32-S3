package io.github.vitorhpsousa.dpfsentinel.core

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

// Port of src/report/regen_watch.cpp: turns the decoded regen flag into start / finish /
// interrupted / still-running messages. A change must hold for a few rows (debounce).
class RegenEvent(val type: Type, val text: String = "") {
    enum class Type { NONE, START, END, INTERRUPTED, TICK }

    companion object {
        val NONE = RegenEvent(Type.NONE)
    }
}

class RegenWatch(private val zone: ZoneId = ZoneId.systemDefault()) {
    private var inRegen = false
    private var activeStreak = 0
    private var inactiveStreak = 0
    private var startMs = 0L
    private var lastValidMs = 0L
    private var lastTickMs = 0L
    private var startWall = 0.0
    private var peakEgt = 0.0
    private var sootStart = 0.0
    private var lastSoot = 0.0

    val active get() = inRegen
    val startedAtMs get() = startMs

    private fun fmt(v: Double, d: Int) = String.format(Locale.US, "%.${d}f", v)

    private fun clock(wall: Double): String {
        if (wall.isNaN() || wall < 1735689600.0) return ""   // clock not set (before 2025-01-01)
        return " at " + DateTimeFormatter.ofPattern("HH:mm").withZone(zone).format(Instant.ofEpochSecond(wall.toLong()))
    }

    private fun pressure(p: Double) = if (p.isNaN()) "" else ", DPF pressure ${fmt(p, 0)} hPa"

    fun update(nowMs: Long, wallClock: Double, active: Double, egtC: Double, sootG: Double, distMi: Double, pressureHpa: Double): RegenEvent {
        if (active.isNaN()) return RegenEvent.NONE

        // Data resumed after a long gap while a regen was open: engine stopped or the link dropped.
        if (inRegen && nowMs - lastValidMs > GAP_MS) {
            val text = "DPF regeneration was interrupted (engine stopped or signal lost)\n" +
                "ran about ${(lastValidMs - startMs) / 60000} min, peak EGT ${fmt(peakEgt, 0)} C" + clock(startWall)
            inRegen = false
            activeStreak = 0
            inactiveStreak = 0
            lastValidMs = nowMs
            return RegenEvent(RegenEvent.Type.INTERRUPTED, text)
        }
        lastValidMs = nowMs
        if (!sootG.isNaN()) lastSoot = sootG
        if (inRegen && !egtC.isNaN() && egtC > peakEgt) peakEgt = egtC

        if (active > 0.5) { activeStreak++; inactiveStreak = 0 } else { inactiveStreak++; activeStreak = 0 }

        if (!inRegen && activeStreak >= STREAK_ROWS) {
            inRegen = true
            startMs = nowMs
            lastTickMs = nowMs
            peakEgt = if (egtC.isNaN()) 0.0 else egtC
            sootStart = if (sootG.isNaN()) lastSoot else sootG
            val distStart = if (distMi.isNaN()) 0.0 else distMi
            startWall = wallClock
            val text = "DPF regeneration started\nsoot ${fmt(sootStart, 1)} g, ${fmt(distStart, 1)} mi since the last regen, " +
                "EGT ${fmt(if (egtC.isNaN()) 0.0 else egtC, 0)} C" + pressure(pressureHpa) + clock(startWall)
            return RegenEvent(RegenEvent.Type.START, text)
        }
        if (inRegen && inactiveStreak >= STREAK_ROWS) {
            inRegen = false
            val text = "DPF regeneration finished\ntook about ${(nowMs - startMs) / 60000} min, peak EGT ${fmt(peakEgt, 0)} C\n" +
                "soot ${fmt(sootStart, 1)} g -> ${fmt(if (sootG.isNaN()) lastSoot else sootG, 1)} g" + clock(wallClock)
            return RegenEvent(RegenEvent.Type.END, text)
        }
        if (inRegen && nowMs - lastTickMs >= TICK_MS) {
            lastTickMs = nowMs
            val mins = (nowMs - startMs) / 60000
            val secs = ((nowMs - startMs) / 1000) % 60
            val text = "DPF regen still running -- $mins:${secs.toString().padStart(2, '0')} so far\n" +
                "EGT ${fmt(if (egtC.isNaN()) 0.0 else egtC, 0)} C, soot ${fmt(if (sootG.isNaN()) lastSoot else sootG, 1)} g" +
                pressure(pressureHpa) + clock(wallClock)
            return RegenEvent(RegenEvent.Type.TICK, text)
        }
        return RegenEvent.NONE
    }

    companion object {
        const val STREAK_ROWS = 3
        const val GAP_MS = 120_000L   // signal lost this long mid-regen = interrupted
        const val TICK_MS = 30_000L   // status ping while a regen is running
    }
}
