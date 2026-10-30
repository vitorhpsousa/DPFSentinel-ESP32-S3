package io.github.vitorhpsousa.dpfsentinel.core

import java.time.Instant
import java.time.ZoneId
import java.time.temporal.ChronoUnit

// Port of src/dpf/health.cpp: regen history, regens since an oil-change reference, warm-up times,
// the next-regen estimate and the edge-triggered health alerts. Thresholds are the firmware's
// (guesses lifted from docs/ix35-known-issues.md). Persisted through encode()/decode() so the
// Android side can keep it in SharedPreferences.
class HealthState {
    class RegenRecord(
        val odometerMi: Double,
        val milesSincePrev: Double,
        var interrupted: Boolean,
        val wallClock: Double,
    )

    val regenHistory = ArrayList<RegenRecord>()   // oldest first, capped at MAX_REGENS
    var regensSinceOil = 0
    var oilChangeOdometerMi = 0.0                 // 0 = unset
    val warmupMinutes = ArrayList<Double>()       // oldest first, capped at MAX_WARMUPS
    var lastWarmupMinutes = Double.NaN
    var lastBatteryV = Double.NaN                 // live, not persisted

    // Next-regen estimate baseline: soot/distance at the start of this cycle (baseSootG NaN = waiting).
    var baseSootG = 0.0
    var baseDistMi = 0.0
    val triggerHist = ArrayList<Double>()         // soot at recent first-in-cycle regen starts
    var cycleHadStart = false
    var lastRateGPerMi = Double.NaN

    // Recomputed every row, not persisted.
    var estMilesToRegen = Double.NaN
    var estFromLiveRate = false
    var estNotSoon = false
    var estRateGPerMi = Double.NaN

    fun encode(): String {
        fun d(v: Double) = v.toString()
        val lines = mutableListOf(
            "ver=$LAYOUT_VERSION",
            "regens=" + regenHistory.joinToString(";") { "${d(it.odometerMi)},${d(it.milesSincePrev)},${it.interrupted},${d(it.wallClock)}" },
            "sinceOil=$regensSinceOil",
            "oilOdo=${d(oilChangeOdometerMi)}",
            "warmups=" + warmupMinutes.joinToString(";") { d(it) },
            "lastWarm=${d(lastWarmupMinutes)}",
            "baseSoot=${d(baseSootG)}",
            "baseDist=${d(baseDistMi)}",
            "triggers=" + triggerHist.joinToString(";") { d(it) },
            "cycStart=$cycleHadStart",
            "lastRate=${d(lastRateGPerMi)}",
        )
        return lines.joinToString("\n")
    }

    companion object {
        const val MAX_REGENS = 20
        const val MAX_WARMUPS = 10
        const val MAX_TRIGGERS = 5
        private const val LAYOUT_VERSION = 1

        // A blob from another layout, or a corrupt one, starts clean rather than half-loads.
        fun decode(text: String?): HealthState {
            val s = HealthState()
            if (text.isNullOrBlank()) return s
            return try {
                val m = text.lines().filter { '=' in it }.associate { it.substringBefore('=') to it.substringAfter('=') }
                if (m["ver"] != LAYOUT_VERSION.toString()) return s
                fun list(key: String) = m[key].orEmpty().split(';').filter { it.isNotEmpty() }
                list("regens").takeLast(MAX_REGENS).forEach {
                    val p = it.split(',')
                    s.regenHistory.add(RegenRecord(p[0].toDouble(), p[1].toDouble(), p[2].toBoolean(), p[3].toDouble()))
                }
                s.regensSinceOil = m["sinceOil"]?.toInt() ?: 0
                s.oilChangeOdometerMi = m["oilOdo"]?.toDouble() ?: 0.0
                list("warmups").takeLast(MAX_WARMUPS).forEach { s.warmupMinutes.add(it.toDouble()) }
                s.lastWarmupMinutes = m["lastWarm"]?.toDouble() ?: Double.NaN
                s.baseSootG = m["baseSoot"]?.toDouble() ?: 0.0
                s.baseDistMi = m["baseDist"]?.toDouble() ?: 0.0
                list("triggers").takeLast(MAX_TRIGGERS).forEach { s.triggerHist.add(it.toDouble()) }
                s.cycleHadStart = m["cycStart"]?.toBoolean() ?: false
                s.lastRateGPerMi = m["lastRate"]?.toDouble() ?: Double.NaN
                s
            } catch (e: RuntimeException) {
                HealthState()
            }
        }
    }
}

enum class HealthAlert(val text: String) {
    FAST_REGENS("Health: 3 regens in a row under 100 mi apart -- oil dilution or a stuck EGR/DPF sensor is worth checking"),
    OIL_REMINDER("Health: 15 regens since the last oil-change reference -- check the dipstick (tap 'Oil changed' once it's done)"),
    SLOW_WARMUP("Health: warm-up to 80C took well over the usual time -- possible stuck-open thermostat"),
}

class HealthTracker(val state: HealthState = HealthState(), private val zone: ZoneId = ZoneId.systemDefault()) {
    private val h = state

    private var fastRegenCond = false
    private var oilCond = false
    private var slowWarmupCond = false
    private val pending = LinkedHashSet<HealthAlert>()

    private var warming = false
    private var warmStartMs = 0L
    private var lastDist = Double.NaN
    private var resetRows = 0
    private var behindRows = 0

    /** Set whenever something persistent changed; the caller saves state.encode() and clears it. */
    var dirty = false

    fun takeAlerts(): List<HealthAlert> = pending.toList().also { pending.clear() }

    fun oilChangeNow(currentOdometerMi: Double) {
        h.oilChangeOdometerMi = if (currentOdometerMi.isNaN()) 0.0 else currentOdometerMi
        h.regensSinceOil = 0
        dirty = true
    }

    fun warmupMedianMin(): Double = median(h.warmupMinutes)

    fun avgRegenIntervalMi(): Double {
        val v = h.regenHistory.map { it.milesSincePrev }.filter { !it.isNaN() }
        return if (v.isEmpty()) Double.NaN else v.average()
    }

    fun regenTriggerG(): Double {
        if (h.triggerHist.isEmpty()) return DEFAULT_TRIGGER_G
        return median(h.triggerHist).coerceIn(TRIGGER_MIN_G, TRIGGER_MAX_G)
    }

    fun regensToday(nowUnix: Double): Int {
        val dayStart = Instant.ofEpochSecond(nowUnix.toLong()).atZone(zone).truncatedTo(ChronoUnit.DAYS).toEpochSecond().toDouble()
        return h.regenHistory.count { it.wallClock >= dayStart }
    }

    fun regensThisWeek(nowUnix: Double): Int = h.regenHistory.count { it.wallClock >= nowUnix - 7 * 86400.0 }

    private fun liveRate(soot: Double, dist: Double): Double {
        if (soot.isNaN() || dist.isNaN() || h.baseSootG.isNaN()) return Double.NaN
        val span = dist - h.baseDistMi
        if (span < MIN_LIVE_SPAN_MI) return Double.NaN
        val rate = (soot - h.baseSootG) / span
        return if (rate >= MIN_LIVE_RATE) rate else Double.NaN
    }

    // Best first: a soot rate (this cycle's once it has enough miles, else the previous cycle's),
    // then (trigger - soot now) / rate; fallback: historical average interval minus miles so far.
    private fun updateEstimate(soot: Double, dist: Double) {
        var miles = Double.NaN
        var usedRate = Double.NaN
        var fromRate = false
        if (!dist.isNaN()) {
            usedRate = liveRate(soot, dist)
            if (usedRate.isNaN()) usedRate = h.lastRateGPerMi
            if (!usedRate.isNaN() && !soot.isNaN()) {
                miles = ((regenTriggerG() - soot) / usedRate).coerceIn(0.0, MAX_ESTIMATE_MI)
                fromRate = true
            } else {
                usedRate = Double.NaN
                val avg = avgRegenIntervalMi()
                if (!avg.isNaN()) miles = (avg - dist).coerceIn(0.0, MAX_ESTIMATE_MI)
            }
        }
        h.estRateGPerMi = usedRate
        h.estFromLiveRate = fromRate
        h.estNotSoon = !soot.isNaN() && soot < SHOW_FROM_SOOT_G
        h.estMilesToRegen = miles
    }

    private fun evalFastRegen() {
        val last3 = h.regenHistory.takeLast(3)
        val cond = last3.size == 3 && last3.all { !it.milesSincePrev.isNaN() && it.milesSincePrev < FAST_REGEN_THRESHOLD_MI }
        if (cond && !fastRegenCond) pending.add(HealthAlert.FAST_REGENS)
        fastRegenCond = cond
    }

    private fun evalOilReminder() {
        val cond = h.regensSinceOil >= OIL_REMINDER_REGENS
        if (cond && !oilCond) pending.add(HealthAlert.OIL_REMINDER)
        oilCond = cond
    }

    private fun evalSlowWarmup() {
        val median = warmupMedianMin()
        val cond = h.warmupMinutes.size >= SLOW_WARMUP_MIN_SAMPLES && !median.isNaN() &&
            !h.lastWarmupMinutes.isNaN() && h.lastWarmupMinutes > median * SLOW_WARMUP_RATIO
        if (cond && !slowWarmupCond) pending.add(HealthAlert.SLOW_WARMUP)
        slowWarmupCond = cond
    }

    /** Call once per assembled row with the RegenEvent that row's RegenWatch.update() returned. */
    fun update(row: Row, event: RegenEvent, nowMs: Long, wallClock: Double) {
        var changed = false
        if (!row.batteryV.isNaN()) h.lastBatteryV = row.batteryV

        val soot = row.soot
        val dist = row.sinceRegen
        // dist_since_regen dropping to ~0 with a near-empty filter, two rows running: a regen completed.
        if (!dist.isNaN()) {
            val dropped = (!lastDist.isNaN() && dist + 0.5 < lastDist) || dist + 0.5 < h.baseDistMi
            val looksReset = dist < RESET_MAX_DIST_MI && (soot.isNaN() || soot < RESET_MAX_SOOT_G)
            resetRows = if (looksReset && (dropped || resetRows > 0)) resetRows + 1 else 0
            // Below the baseline but well past 0: a regen completed while the phone wasn't logging.
            val behind = dist + 0.5 < h.baseDistMi && !looksReset
            behindRows = if (behind) behindRows + 1 else 0
            if (resetRows == RESET_CONFIRM_ROWS) {
                h.baseDistMi = dist
                h.baseSootG = Double.NaN
                h.cycleHadStart = false
                changed = true
            } else if (behindRows == RESET_CONFIRM_ROWS) {
                h.baseDistMi = 0.0
                h.baseSootG = 0.0
                h.cycleHadStart = false
                changed = true
            }
            lastDist = dist
        }
        // Soot is polled at a different rate from distance, so it can be missing on the reset row.
        if (h.baseSootG.isNaN() && !soot.isNaN() && !dist.isNaN()) {
            h.baseSootG = if (dist - h.baseDistMi < 2.0) soot else 0.0
            changed = true
        }

        if (event.type == RegenEvent.Type.START) {
            // Only the first start of a cycle teaches the trigger and the rate; later ones are retries.
            if (!h.cycleHadStart) {
                val r = liveRate(soot, dist)
                if (!r.isNaN()) h.lastRateGPerMi = r
                if (!soot.isNaN()) {
                    h.triggerHist.add(soot)
                    while (h.triggerHist.size > HealthState.MAX_TRIGGERS) h.triggerHist.removeAt(0)
                }
            }
            h.cycleHadStart = true
            h.regenHistory.add(HealthState.RegenRecord(row.odometer, row.sinceRegen, false, wallClock))
            while (h.regenHistory.size > HealthState.MAX_REGENS) h.regenHistory.removeAt(0)
            h.regensSinceOil++
            changed = true
            evalFastRegen()
        } else if (event.type == RegenEvent.Type.INTERRUPTED) {
            h.regenHistory.lastOrNull()?.let { if (!it.interrupted) { it.interrupted = true; changed = true } }
            // An aborted burn drops soot without resetting distance: measure on from here.
            if (!dist.isNaN()) {
                h.baseDistMi = dist
                h.baseSootG = soot
                changed = true
            }
        }

        val coolant = row.coolant
        if (!coolant.isNaN()) {
            if (!warming && coolant < WARMUP_COLD_C) {
                warming = true
                warmStartMs = nowMs
            } else if (warming && coolant >= WARMUP_DONE_C) {
                val mins = (nowMs - warmStartMs) / 60000.0
                h.warmupMinutes.add(mins)
                while (h.warmupMinutes.size > HealthState.MAX_WARMUPS) h.warmupMinutes.removeAt(0)
                h.lastWarmupMinutes = mins
                warming = false
                changed = true
                evalSlowWarmup()
            }
        }

        updateEstimate(soot, dist)
        evalOilReminder()
        if (changed) dirty = true
    }

    companion object {
        const val FAST_REGEN_THRESHOLD_MI = 100.0
        const val OIL_REMINDER_REGENS = 15
        const val SLOW_WARMUP_RATIO = 1.3
        const val SLOW_WARMUP_MIN_SAMPLES = 3
        const val WARMUP_COLD_C = 40.0
        const val WARMUP_DONE_C = 80.0

        // Regens on this car have started at 17.65-19.61 g; the lowest is the default so the estimate errs "sooner".
        const val DEFAULT_TRIGGER_G = 17.65
        const val TRIGGER_MIN_G = 16.5
        const val TRIGGER_MAX_G = 20.0
        const val RESET_MAX_DIST_MI = 1.0
        const val RESET_MAX_SOOT_G = 6.0
        const val RESET_CONFIRM_ROWS = 2
        const val MIN_LIVE_SPAN_MI = 20.0
        const val MIN_LIVE_RATE = 0.02
        const val MAX_ESTIMATE_MI = 999.0
        // Below this the estimate was typically ~30 mi off over 12 logged cycles: show "not soon".
        const val SHOW_FROM_SOOT_G = 12.0

        fun median(v: List<Double>): Double {
            if (v.isEmpty()) return Double.NaN
            val s = v.sorted()
            val n = s.size
            return if (n % 2 == 1) s[n / 2] else (s[n / 2 - 1] + s[n / 2]) / 2.0
        }
    }
}
