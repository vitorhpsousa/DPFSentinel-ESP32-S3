package io.github.vitorhpsousa.dpfsentinel

import io.github.vitorhpsousa.dpfsentinel.core.Row
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

// Everything the phone screen and the car screen show, published once a second by LoggerService.
data class Snapshot(
    val running: Boolean = false,
    val status: String = "Not logging",
    val connected: Boolean = false,
    val adapterName: String = "",
    val row: Row? = null,
    val stale: Boolean = true,
    val regenActive: Boolean = false,
    val regenStartedAtMs: Long = 0,
    val estimate: String = "--",
    val estMiles: Double = Double.NaN,
    val estNotSoon: Boolean = false,
    val estFromLiveRate: Boolean = false,
    val triggerG: Double = Double.NaN,
    val rateGPerMi: Double = Double.NaN,
    val sootZone: Int = 0,
    val history: List<HistoryPoint> = emptyList(),
    val health: HealthSummary = HealthSummary(),
    val session: Int = 0,
    val rows: Long = 0,
    val lastEvent: String = "",
    val idleBlockage: Boolean = false,
)

data class HistoryPoint(val ms: Long, val soot: Double, val diffPressure: Double, val egt: Double, val rpm: Double)

data class HealthSummary(
    val regensToday: Int = 0,
    val regensThisWeek: Int = 0,
    val avgIntervalMi: Double = Double.NaN,
    val regensSinceOil: Int = 0,
    val oilChangeOdoMi: Double = 0.0,
    val lastWarmupMin: Double = Double.NaN,
    val warmupMedianMin: Double = Double.NaN,
    val batteryV: Double = Double.NaN,
)

object LiveState {
    private val _state = MutableStateFlow(Snapshot())
    val state: StateFlow<Snapshot> = _state.asStateFlow()

    fun set(s: Snapshot) { _state.value = s }
    fun update(f: (Snapshot) -> Snapshot) = _state.update(f)
}
