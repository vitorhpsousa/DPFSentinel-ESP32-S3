package io.github.vitorhpsousa.dpfsentinel.core

import kotlin.math.abs

// Phone-only columns appended after the firmware's, so tools/*.py (which read by column name)
// still work and the firmware header stays a prefix of the phone's. NaN = no reading.
object PhoneColumns {
    val NAMES = listOf(
        "gps_lat", "gps_lon", "gps_speed_kmh", "gps_alt_m", "gps_acc_m",
        "baro_hpa", "alt_m", "grade_pct",
    )
    val HEADER: String = CsvFormat.HEADER + "," + NAMES.joinToString(",")

    fun empty() = DoubleArray(NAMES.size) { Double.NaN }

    /** The extra fields, leading comma included; lat/lon get 6 decimals (~0.1 m), the rest 2. */
    fun format(extra: DoubleArray): String {
        val sb = StringBuilder()
        extra.forEachIndexed { i, v ->
            sb.append(',')
            if (!v.isNaN()) sb.append(String.format(java.util.Locale.US, if (i < 2) "%.6f" else "%.2f", v))
        }
        return sb.toString()
    }
}

/**
 * Road grade from altitude against distance travelled.
 *
 * Altitude comes from the phone's barometer (fine-grained, ~0.3 m noise, but drifts with weather and
 * cabin ventilation), pinned to GPS altitude by a slow average of the difference (GPS is absolute but
 * ±10 m noisy). Distance is integrated from speed (OBD speed when available). Grade is the altitude
 * change over the last [windowM] metres, reported only while moving.
 */
class GradeTracker(
    private val windowM: Double = 100.0,
    private val minSpeedKmh: Double = 8.0,
    private val baroAlpha: Double = 0.3,
    private val anchorAlpha: Double = 0.02,
) {
    private data class Point(val distM: Double, val altM: Double)

    private val points = ArrayDeque<Point>()
    private var lastMs = -1L
    private var distM = 0.0
    private var baroSmooth = Double.NaN
    private var offset = Double.NaN   // gps altitude minus barometric altitude

    var altitudeM = Double.NaN
        private set
    var gradePct = Double.NaN
        private set
    val distanceM get() = distM

    /**
     * Feeds one sample (normally once a second).
     * @param baroAltM barometric altitude (standard atmosphere), NaN if the phone has no barometer
     * @param gpsAltM GPS altitude above sea level, NaN without a fix
     * @param gpsAccM vertical accuracy of [gpsAltM] in metres, NaN if unknown
     */
    fun update(ms: Long, speedKmh: Double, baroAltM: Double, gpsAltM: Double, gpsAccM: Double = Double.NaN) {
        val dt = if (lastMs < 0) 0.0 else (ms - lastMs).coerceIn(0, 5000) / 1000.0
        lastMs = ms
        val moving = !speedKmh.isNaN() && speedKmh >= minSpeedKmh
        if (!speedKmh.isNaN()) distM += speedKmh / 3.6 * dt

        val gpsOk = !gpsAltM.isNaN() && (gpsAccM.isNaN() || gpsAccM <= 25.0)
        if (!baroAltM.isNaN()) {
            baroSmooth = if (baroSmooth.isNaN()) baroAltM else baroSmooth + baroAlpha * (baroAltM - baroSmooth)
            if (gpsOk) {
                val d = gpsAltM - baroSmooth
                offset = if (offset.isNaN()) d else offset + anchorAlpha * (d - offset)
            }
            altitudeM = baroSmooth + (if (offset.isNaN()) 0.0 else offset)
        } else {
            altitudeM = if (gpsOk) (if (altitudeM.isNaN()) gpsAltM else altitudeM + 0.2 * (gpsAltM - altitudeM)) else Double.NaN
        }

        if (altitudeM.isNaN() || !moving) {
            gradePct = Double.NaN
            if (altitudeM.isNaN()) points.clear()
            return
        }
        // Use the barometric track itself for the slope (the slow GPS anchor would add a fake grade).
        val track = if (!baroSmooth.isNaN()) baroSmooth else altitudeM
        points.addLast(Point(distM, track))
        while (points.size > 2 && distM - points[1].distM >= windowM) points.removeFirst()
        val first = points.first()
        val run = distM - first.distM
        gradePct = if (run >= windowM * 0.5) {
            val g = (track - first.altM) / run * 100.0
            if (abs(g) > 30.0) Double.NaN else g   // no UK road is steeper; treat as a glitch
        } else Double.NaN
    }

    companion object {
        /** Barometric altitude in metres for a pressure in hPa (standard atmosphere, 1013.25 hPa). */
        fun pressureAltitudeM(hpa: Double): Double =
            if (hpa.isNaN() || hpa <= 0) Double.NaN else 44330.0 * (1.0 - Math.pow(hpa / 1013.25, 1.0 / 5.255))
    }
}
