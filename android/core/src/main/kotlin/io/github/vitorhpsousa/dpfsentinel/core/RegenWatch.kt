package io.github.vitorhpsousa.dpfsentinel.core

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

// Port of src/report/regen_watch.cpp: turns the decoded regen flag into start / finish /
// interrupted / still-running messages. A change must hold for a few rows (debounce).
//
// The ECU drops the regen flag while the car stands at idle (a drive-through queue, traffic) or
// when the engine is switched off, and picks the same regen up again once the car drives on
// (session_59 on 2026-10-25: stopped mid-regen, back on 20 s after pulling away). That is a
// pause, not a finish: it is reported as PAUSED and then RESUMED, and only becomes INTERRUPTED
// when the car has driven [PAUSE_DRIVE_MS] or [PAUSE_MAX_MS] has passed without it coming back.
class RegenEvent(val type: Type, val text: String = "") {
    enum class Type { NONE, START, END, INTERRUPTED, TICK, PAUSED, RESUMED }

    companion object {
        val NONE = RegenEvent(Type.NONE)
    }
}

// units: read on every message, so a change in the settings applies to the next one.
class RegenWatch(private val zone: ZoneId = ZoneId.systemDefault(), private val units: () -> Units = { Units.UK }) {
    private var inRegen = false
    private var isPaused = false
    private var activeStreak = 0
    private var inactiveStreak = 0
    private var startMs = 0L
    private var lastValidMs = 0L
    private var lastTickMs = 0L
    private var pausedAtMs = 0L
    private var movingWhilePausedMs = 0L
    private var startWall = 0.0
    private var peakEgt = 0.0
    private var sootStart = 0.0
    private var lastSoot = 0.0
    private var haveSoot = false

    /** A regen is open: running or paused. */
    val active get() = inRegen
    /** The open regen is paused (car stopped, engine off or signal lost) and may still resume. */
    val paused get() = inRegen && isPaused
    val startedAtMs get() = startMs

    private fun fmt(v: Double, d: Int) = String.format(Locale.US, "%.${d}f", v)

    private fun clock(wall: Double): String {
        if (wall.isNaN() || wall < 1735689600.0) return ""   // clock not set (before 2025-01-01)
        return " at " + DateTimeFormatter.ofPattern("HH:mm").withZone(zone).format(Instant.ofEpochSecond(wall.toLong()))
    }

    private fun pressure(p: Double) = if (p.isNaN()) "" else ", DPF pressure ${fmt(p, 0)} hPa"

    private fun pause(atMs: Long) {
        isPaused = true
        pausedAtMs = atMs
        movingWhilePausedMs = 0
    }

    // Closes a paused regen that never came back: finished if the filter is (nearly) empty.
    private fun close(wallClock: Double, soot: Double, emptyFilter: Boolean): RegenEvent {
        inRegen = false
        isPaused = false
        val ran = (pausedAtMs - startMs) / 60000
        val tail = "ran about $ran min, peak EGT ${units().temp(peakEgt)}\nsoot ${fmt(sootStart, 1)} g -> ${fmt(soot, 1)} g" + clock(wallClock)
        return if (emptyFilter) RegenEvent(RegenEvent.Type.END, "DPF regeneration finished\n$tail")
        else RegenEvent(RegenEvent.Type.INTERRUPTED, "DPF regeneration stopped before finishing\n$tail")
    }

    /** speedKmh: NaN when unknown, which keeps the old behaviour (a dropped flag = finished). */
    fun update(
        nowMs: Long, wallClock: Double, active: Double, egtC: Double, sootG: Double, distMi: Double, pressureHpa: Double,
        speedKmh: Double = Double.NaN,
    ): RegenEvent {
        if (active.isNaN()) return RegenEvent.NONE

        // Data resumed after a long gap while a regen was running: engine stopped or the link
        // dropped. The ECU usually carries the regen on once the car drives again.
        if (inRegen && !isPaused && nowMs - lastValidMs > GAP_MS) {
            pause(lastValidMs)
            activeStreak = 0
            inactiveStreak = 0
            lastValidMs = nowMs
            return RegenEvent(
                RegenEvent.Type.PAUSED,
                "DPF regeneration paused (engine stopped or signal lost)\nran about ${(pausedAtMs - startMs) / 60000} min, " +
                    "peak EGT ${units().temp(peakEgt)}. It usually carries on once you drive again" + clock(wallClock),
            )
        }
        val dt = (nowMs - lastValidMs).coerceIn(0, 5000)
        lastValidMs = nowMs
        if (!sootG.isNaN()) { lastSoot = sootG; haveSoot = true }
        val soot = if (sootG.isNaN()) lastSoot else sootG
        val emptyFilter = haveSoot && soot <= EMPTY_SOOT_G   // unknown soot never counts as empty
        if (inRegen && !egtC.isNaN() && egtC > peakEgt) peakEgt = egtC

        if (active > 0.5) { activeStreak++; inactiveStreak = 0 } else { inactiveStreak++; activeStreak = 0 }

        if (paused) {
            if (activeStreak >= STREAK_ROWS) {
                isPaused = false
                lastTickMs = nowMs
                return RegenEvent(
                    RegenEvent.Type.RESUMED,
                    "DPF regeneration resumed\nafter a ${(nowMs - pausedAtMs) / 60000} min pause, soot ${fmt(soot, 1)} g, " +
                        "EGT ${units().temp(if (egtC.isNaN()) 0.0 else egtC)}" + clock(wallClock),
                )
            }
            if (!speedKmh.isNaN() && speedKmh > STILL_KMH) movingWhilePausedMs += dt
            if (movingWhilePausedMs >= PAUSE_DRIVE_MS || nowMs - pausedAtMs >= PAUSE_MAX_MS) return close(wallClock, soot, emptyFilter)
            return RegenEvent.NONE
        }

        if (!inRegen && activeStreak >= STREAK_ROWS) {
            inRegen = true
            isPaused = false
            startMs = nowMs
            lastTickMs = nowMs
            peakEgt = if (egtC.isNaN()) 0.0 else egtC
            sootStart = soot
            val distStart = if (distMi.isNaN()) 0.0 else distMi
            startWall = wallClock
            val text = "DPF regeneration started\nsoot ${fmt(sootStart, 1)} g, ${units().distance(distStart, 1)} since the last regen, " +
                "EGT ${units().temp(if (egtC.isNaN()) 0.0 else egtC)}" + pressure(pressureHpa) + clock(startWall)
            return RegenEvent(RegenEvent.Type.START, text)
        }
        if (inRegen && inactiveStreak >= STREAK_ROWS) {
            // Flag dropped while standing still with soot left: the ECU is holding the regen.
            val standing = !speedKmh.isNaN() && speedKmh <= STILL_KMH
            if (standing && !emptyFilter) {
                pause(nowMs)
                return RegenEvent(
                    RegenEvent.Type.PAUSED,
                    "DPF regeneration paused while the car is stopped\nran ${(nowMs - startMs) / 60000} min so far, soot ${fmt(soot, 1)} g. " +
                        "It should carry on once you drive off" + clock(wallClock),
                )
            }
            inRegen = false
            val text = "DPF regeneration finished\ntook about ${(nowMs - startMs) / 60000} min, peak EGT ${units().temp(peakEgt)}\n" +
                "soot ${fmt(sootStart, 1)} g -> ${fmt(soot, 1)} g" + clock(wallClock)
            return RegenEvent(RegenEvent.Type.END, text)
        }
        if (inRegen && nowMs - lastTickMs >= TICK_MS) {
            lastTickMs = nowMs
            val mins = (nowMs - startMs) / 60000
            val secs = ((nowMs - startMs) / 1000) % 60
            val text = "DPF regen still running -- $mins:${secs.toString().padStart(2, '0')} so far\n" +
                "EGT ${units().temp(if (egtC.isNaN()) 0.0 else egtC)}, soot ${fmt(soot, 1)} g" +
                pressure(pressureHpa) + clock(wallClock)
            return RegenEvent(RegenEvent.Type.TICK, text)
        }
        return RegenEvent.NONE
    }

    companion object {
        const val STREAK_ROWS = 3
        const val GAP_MS = 120_000L          // no data this long mid-regen = engine off or link lost: paused
        const val TICK_MS = 30_000L          // status ping while a regen is running
        const val STILL_KMH = 3.0            // at or below this the car counts as standing
        const val EMPTY_SOOT_G = 3.0         // a flag drop at or below this soot is a real finish
        const val PAUSE_DRIVE_MS = 3 * 60_000L   // driving this long without it resuming = stopped
        const val PAUSE_MAX_MS = 20 * 60_000L    // ... or this long paused in all
    }
}
