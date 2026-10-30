package io.github.vitorhpsousa.dpfsentinel

import android.content.Context
import io.github.vitorhpsousa.dpfsentinel.core.CsvFormat
import java.io.BufferedWriter
import java.io.File
import java.io.FileWriter

// session_N.csv + raw_N.log, same names and formats as the firmware's /obd folder, so the
// tools/*.py helpers work on them. Kept in the app's external files folder
// (Android/data/io.github.vitorhpsousa.dpfsentinel/files/obd), shareable from the Logs tab.
class SessionFiles(context: Context, val number: Int) {
    val dir: File = folder(context)
    private val csv: BufferedWriter
    private val raw: BufferedWriter
    var rows = 0L
        private set

    init {
        dir.mkdirs()
        val csvFile = File(dir, "session_$number.csv")
        val fresh = !csvFile.exists() || csvFile.length() == 0L
        csv = BufferedWriter(FileWriter(csvFile, true))
        if (fresh) csv.write(CsvFormat.HEADER + "\n")
        raw = BufferedWriter(FileWriter(File(dir, "raw_$number.log"), true))
        raw.write(CsvFormat.timeMarker(0, System.currentTimeMillis() / 1000) + "\n")
    }

    fun writeRow(ms: Long, unixSec: Double, values: DoubleArray) {
        runCatching {
            csv.write(CsvFormat.row(ms, unixSec, values) + "\n")
            csv.flush()
            rows++
        }
    }

    fun raw(ms: Long, header: String, request: String, reply: String) {
        runCatching { raw.write(CsvFormat.rawLine(ms, header, request, reply) + "\n") }
    }

    fun flushRaw() { runCatching { raw.flush() } }

    fun close() {
        runCatching { csv.close() }
        runCatching { raw.close() }
    }

    companion object {
        fun folder(context: Context): File = File(context.getExternalFilesDir(null) ?: context.filesDir, "obd")

        private val NUM = Regex("""^(session|raw)_(\d+)\.(csv|log)$""")

        fun numberOf(f: File): Int = NUM.find(f.name)?.groupValues?.get(2)?.toIntOrNull() ?: -1

        /** Log files, newest session first. */
        fun list(context: Context): List<File> =
            (folder(context).listFiles()?.filter { numberOf(it) >= 0 } ?: emptyList())
                .sortedWith(compareByDescending<File> { numberOf(it) }.thenBy { it.name })
    }
}
