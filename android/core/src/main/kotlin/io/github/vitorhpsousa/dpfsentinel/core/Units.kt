package io.github.vitorhpsousa.dpfsentinel.core

import java.util.Locale

// How values are shown to a person. The ECU reports speed in km/h and distances in miles (the
// firmware decodes them that way); the CSV logs keep those raw units whatever is picked here.
enum class Units(val label: String) {
    UK("UK: mph, miles, °C"),
    METRIC("Metric: km/h, km, °C"),
    US("US: mph, miles, °F");

    private val metricDistance get() = this == METRIC

    val speedUnit get() = if (metricDistance) "km/h" else "mph"
    val distanceUnit get() = if (metricDistance) "km" else "mi"
    val tempUnit get() = if (this == US) "°F" else "°C"

    fun speedOf(kmh: Double): Double = if (metricDistance) kmh else kmh / KM_PER_MI
    fun distanceOf(mi: Double): Double = if (metricDistance) mi * KM_PER_MI else mi
    fun tempOf(c: Double): Double = if (this == US) c * 9.0 / 5.0 + 32.0 else c
    /** A temperature difference or threshold, e.g. "80 °C" -> "176 °F". */
    fun tempNumber(c: Double): String = num(tempOf(c), 0)

    fun speed(kmh: Double, d: Int = 0) = "${num(speedOf(kmh), d)} $speedUnit"
    fun distance(mi: Double, d: Int = 0) = "${num(distanceOf(mi), d)} $distanceUnit"
    fun temp(c: Double, d: Int = 0) = "${num(tempOf(c), d)} $tempUnit"
    /** Soot accumulation, stored per mile. */
    fun sootRate(gPerMi: Double) = "${num(if (metricDistance) gPerMi / KM_PER_MI else gPerMi, 3)} g/$distanceUnit"

    companion object {
        const val KM_PER_MI = 1.609344

        fun num(v: Double, d: Int): String = if (v.isNaN()) "--" else String.format(Locale.US, "%.${d}f", v)

        fun fromName(name: String?): Units = entries.firstOrNull { it.name == name } ?: UK
    }
}
