package io.github.vitorhpsousa.dpfsentinel.core

// Port of src/obd/isotp.cpp. Reassembles an ISO-TP payload from ELM327 text with headers on (ATH1)
// and spaces off (ATS0): frames separated by '\r', each a 3-char CAN id plus up to 8 data bytes.
// Only frames from rxId are used. Returns the payload starting at the service id (0x41.. / 0x61..)
// as unsigned bytes (0..255), or an empty array if the reply is missing, foreign or incomplete.
object IsoTp {
    private fun hexVal(c: Char): Int = Character.digit(c, 16)

    private class Frame(val id: String, val data: IntArray)

    private fun parseLine(line: String): Frame? {
        val clean = line.replace(" ", "").uppercase()
        if (clean.length < 5 || (clean.length - 3) % 2 != 0 || clean.length > 47) return null
        if (clean.any { hexVal(it) < 0 }) return null
        val bytes = (clean.length - 3) / 2
        if (bytes > 16) return null
        val data = IntArray(bytes) { (hexVal(clean[3 + 2 * it]) shl 4) or hexVal(clean[4 + 2 * it]) }
        return Frame(clean.substring(0, 3), data)
    }

    fun extract(text: String, rxId: String, cap: Int = 96): IntArray {
        var gotFirst = false
        var total = 0
        var got = 0
        val out = ArrayList<Int>()

        for (line in text.split('\r', '\n')) {
            val f = parseLine(line) ?: continue
            val b = f.data
            if (b.size < 2 || f.id != rxId) continue
            val pci = b[0] shr 4
            if (!gotFirst) {
                if (pci == 0) {
                    val len = b[0] and 0x0F
                    if (len == 0 || b.size < 1 + len) return IntArray(0)
                    return b.copyOfRange(1, 1 + minOf(len, cap))
                }
                if (pci != 1) continue
                total = ((b[0] and 0x0F) shl 8) or b[1]
                gotFirst = true
                for (i in 2 until b.size) { if (out.size < cap) out.add(b[i]); got++ }
            } else if (pci == 2) {
                for (i in 1 until b.size) { if (out.size < cap) out.add(b[i]); got++ }
            }
            if (gotFirst && got >= total) return out.take(minOf(out.size, total)).toIntArray()
        }
        return IntArray(0)
    }
}
