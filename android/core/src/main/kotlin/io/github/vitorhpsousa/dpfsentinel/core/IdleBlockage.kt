package io.github.vitorhpsousa.dpfsentinel.core

/**
 * "DPF pressure high at idle", the possible-blockage sign. Twin of src/dpf/dpf_monitor.cpp.
 *
 * Idle DPF pressure rises with the soot load: on this ix35 a normal 10-18 g filter idles at
 * about 22 hPa (median), and during a regen idle pressure is 20+ hPa most of the time. The old
 * single-row "20 hPa at idle" test therefore fired on every loaded filter and through every regen.
 * Now it needs a warm engine at idle, no regen running (or paused) and none in the last 10 minutes,
 * and [THRESHOLD_HPA] held for [HOLD_MS]. Across the logs up to 2026-11-07 (123 sessions) that
 * never fires; 25 hPa would have fired in 3 sessions.
 */
class IdleBlockageWatch {
    private var aboveSince = -1L
    private var lastRegenMs = Long.MIN_VALUE / 4

    var flagged = false
        private set

    fun update(nowMs: Long, rpm: Double, speedKmh: Double, dpHpa: Double, coolantC: Double, regenBusy: Boolean): Boolean {
        if (regenBusy) lastRegenMs = nowMs
        val idle = !rpm.isNaN() && rpm >= MIN_RPM && rpm <= IDLE_RPM_CEILING && (speedKmh.isNaN() || speedKmh < 1.0)
        val eligible = idle && !dpHpa.isNaN() && !coolantC.isNaN() && coolantC >= WARM_C &&
            !regenBusy && nowMs - lastRegenMs >= AFTER_REGEN_MS
        if (!eligible || dpHpa < THRESHOLD_HPA) {
            aboveSince = -1
            flagged = false
            return false
        }
        if (aboveSince < 0) aboveSince = nowMs
        flagged = nowMs - aboveSince >= HOLD_MS
        return flagged
    }

    companion object {
        const val THRESHOLD_HPA = 35.0
        const val HOLD_MS = 60_000L
        const val WARM_C = 70.0
        const val AFTER_REGEN_MS = 10 * 60_000L
        const val IDLE_RPM_CEILING = 1100.0
        const val MIN_RPM = 500.0   // below this the engine is stopping or cranking
    }
}
