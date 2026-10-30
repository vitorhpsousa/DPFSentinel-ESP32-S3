package io.github.vitorhpsousa.dpfsentinel.core

import java.util.Locale

// Same file layout as the firmware (docs/log-format.md), so tools/*.py work on phone logs unchanged.
object CsvFormat {
    val HEADER: String = (listOf("millis", "unix_time") + Pids.COLUMNS).joinToString(",")

    private fun num(v: Double, decimals: Int) = String.format(Locale.US, "%.${decimals}f", v)

    fun row(ms: Long, unixSec: Double, values: DoubleArray): String {
        val sb = StringBuilder()
        sb.append(ms).append(',')
        if (!unixSec.isNaN()) sb.append(num(unixSec, 3))
        for (v in values) {
            sb.append(',')
            if (!v.isNaN()) sb.append(num(v, 3))
        }
        return sb.toString()
    }

    // raw_N.log line: millis, header, request, reply with line breaks shown as '|'.
    fun rawLine(ms: Long, header: String, request: String, reply: String): String =
        "$ms\t$header\t$request\t${reply.replace('\r', '|').replace('\n', '|')}"

    // Marker tools/date_session.py uses to date rows; the phone always knows the time, source "phone".
    fun timeMarker(ms: Long, unixSec: Long): String = "$ms\tTIME\tphone\t$unixSec"
}
