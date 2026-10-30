package io.github.vitorhpsousa.dpfsentinel.core

// The adapter link: send one ELM327 command (without '\r') and return the reply up to the '>'
// prompt with its '\r' frame separators intact, trimmed. Empty string = no reply / not connected.
interface ElmTransport {
    fun send(cmd: String, timeoutMs: Long = 3000): String
}

// Port of the request scheduling in src/main.cpp + ElmClient::initAdapter/setHeader. One entry per
// unique (header, request): several PIDs share the 2103 reply, so it goes out once and every
// column decodes from the same payload. The most overdue request is served each step.
class ObdPoller(private val elm: ElmTransport, private val raw: (header: String, request: String, reply: String) -> Unit = { _, _, _ -> }) {
    private class Req(val header: String, val request: String, val frames: String, val rxId: String, var periodMs: Long, val prefix: IntArray) {
        var nextDue = 0L
        var lastGood = -1L   // -1 = never answered
        var fails = 0
        var noCount = false   // adapter rejected the frame-count digit
        var payload = IntArray(0)
    }

    private val reqs = ArrayList<Req>()
    private val reqOf = IntArray(Pids.TABLE.size)
    private var currentHeader = ""

    init {
        Pids.TABLE.forEachIndexed { i, p ->
            var found = reqs.indexOfFirst { it.header == p.header && it.request == p.request }
            if (found < 0) {
                reqs.add(Req(p.header, p.request, p.frames, Pids.rxIdFor(p.header), p.periodMs, p.prefix))
                found = reqs.size - 1
            } else if (p.periodMs < reqs[found].periodMs) {
                reqs[found].periodMs = p.periodMs
            }
            reqOf[i] = found
        }
    }

    /** Reset, echo/linefeeds off, headers ON, spaces off, ISO 15765-4 CAN 11/500, 200 ms reply timeout. */
    fun initAdapter(nowMs: Long): Boolean {
        currentHeader = ""
        elm.send("ATZ", 3000)
        for (s in listOf("ATE0", "ATL0", "ATH1", "ATS0", "ATSP6", "ATST32")) {
            if (elm.send(s).isEmpty()) return false
        }
        for (r in reqs) { r.nextDue = nowMs; r.fails = 0 }
        return true
    }

    private fun setHeader(header: String): Boolean {
        if (currentHeader == header) return true
        if (!elm.send("ATSH$header").contains("OK")) return false
        currentHeader = header
        return true
    }

    private fun run(r: Req, nowMs: Long, keepRaw: Boolean) {
        var ok = false
        if (setHeader(r.header)) {
            var frames = if (r.noCount) "" else r.frames
            var reply = elm.send(r.request + frames)
            // Some clones answer "?" to the frame-count digit: retry without it and remember.
            if (!r.noCount && r.frames.isNotEmpty() && reply.contains('?')) {
                r.noCount = true
                frames = ""
                reply = elm.send(r.request)
            }
            if (keepRaw) raw(r.header, r.request + frames, reply)
            val p = IsoTp.extract(reply, r.rxId)
            if (p.size >= r.prefix.size && r.prefix.indices.all { p[it] == r.prefix[it] }) {
                r.payload = p
                r.lastGood = nowMs
                r.fails = 0
                ok = true
            }
        }
        if (!ok && r.fails < 255) r.fails++
        // Requests the ECU never answers back off to one try every 30 s.
        r.nextDue = nowMs + if (r.fails >= 5) 30_000L else r.periodMs
    }

    /** Serves the most overdue request, if any. keepRaw: whether its exchange goes to the raw log. */
    fun step(nowMs: Long, keepRaw: Boolean): Boolean {
        val best = reqs.filter { nowMs >= it.nextDue }.minByOrNull { it.nextDue } ?: return false
        run(best, nowMs, keepRaw)
        return true
    }

    fun assembleRow(nowMs: Long): Row {
        val row = Row()
        Pids.TABLE.forEachIndexed { i, p ->
            val r = reqs[reqOf[i]]
            val stale = maxOf(r.periodMs * 3, 3000L)
            row.values[i] = if (r.lastGood >= 0 && nowMs - r.lastGood <= stale) p.decode(r.payload) else Double.NaN
        }
        val odo = row.odometer
        val dist = row.sinceRegen
        row.values[Pids.col("dpf_odo_at_last_regen_mi")] = if (!odo.isNaN() && !dist.isNaN()) odo - dist else Double.NaN
        return row
    }
}

// Soot beeps/notifications: amber >= 14 g, red >= 17 g, with 0.5 g hysteresis so it doesn't chatter.
class SootZone {
    var zone = 0
        private set

    /** Returns the new zone when it rose (1 amber, 2 red), else null. */
    fun update(soot: Double): Int? {
        if (soot.isNaN()) return null
        var z = zone
        if (soot >= RED_G) z = 2 else if (soot >= AMBER_G && z < 1) z = 1
        if (z == 2 && soot < RED_G - 0.5) z = if (soot >= AMBER_G) 1 else 0
        if (z == 1 && soot < AMBER_G - 0.5) z = 0
        val rose = z > zone
        zone = z
        return if (rose) z else null
    }

    companion object {
        const val AMBER_G = 14.0
        const val RED_G = 17.0
    }
}
