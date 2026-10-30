package io.github.vitorhpsousa.dpfsentinel.core

import java.util.Locale

// Telegram texts, worded like the firmware's (src/report/telegram_report.cpp).
object Messages {
    const val KOFI_URL = "https://ko-fi.com/vitoi"

    private fun n(v: Double, d: Int) = if (v.isNaN()) "--" else String.format(Locale.US, "%.${d}f", v)

    fun latestLine(row: Row?): String =
        if (row == null || row.soot.isNaN()) "No live engine reading right now (car off or adapter out of range)."
        else "Latest: soot ${n(row.soot, 1)} g, ${n(row.sinceRegen, 1)} mi since regen, DPF ${n(row.diffPressure, 0)} hPa, " +
            "EGT ${n(row.egt, 0)} C, battery ${n(row.batteryV, 1)} V"

    fun status(time: String, row: Row?, estimate: String): String {
        if (row == null || row.soot.isNaN()) return "Status $time: car off or adapter out of range"
        return "Status $time: rpm ${n(row.rpm, 0)}, speed ${n(row.speed, 0)} km/h\n" +
            "soot ${n(row.soot, 1)} g, ${n(row.sinceRegen, 1)} mi since regen\n" +
            "DPF ${n(row.diffPressure, 0)} hPa, EGT ${n(row.egt, 0)} C, battery ${n(row.batteryV, 1)} V\n" +
            "next regen: $estimate"
    }

    fun dailyReport(date: String, fileCount: Int, bytes: Long, latestLine: String): String =
        "DPF Sentinel (phone) report, $date\n$fileCount file(s), ${fmtBytes(bytes)} to send\n$latestLine\n\nFound this useful? $KOFI_URL"

    fun fmtBytes(b: Long): String = if (b > 1_048_576) String.format(Locale.US, "%.1f MB", b / 1_048_576.0) else "${b / 1024} KB"

    /** Next-regen text as the screen shows it: "not soon", "~42 mi", "due now", or "--". */
    fun estimate(h: HealthState): String = when {
        h.estMilesToRegen.isNaN() -> "--"
        h.estNotSoon -> "not soon"
        h.estMilesToRegen < 1.0 -> "due now"
        else -> "~${h.estMilesToRegen.toInt()} mi" + if (h.estFromLiveRate) "" else " (avg)"
    }
}
