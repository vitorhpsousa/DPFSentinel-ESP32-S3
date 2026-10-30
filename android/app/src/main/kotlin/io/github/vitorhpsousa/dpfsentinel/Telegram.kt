package io.github.vitorhpsousa.dpfsentinel

import android.content.Context
import android.util.Log
import io.github.vitorhpsousa.dpfsentinel.core.Messages
import org.json.JSONObject
import java.io.DataOutputStream
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.ArrayDeque
import java.util.Locale
import java.util.concurrent.Executors

// The phone's version of src/report/telegram_report.cpp: queued alerts (delivered once the phone is
// online, oldest dropped past 10), a daily report after the set time that uploads the session logs
// not sent yet, and replies to "/status" from the configured chat. Off until a token and chat id are set.
class Telegram(context: Context) {
    private val ctx = context.applicationContext
    private val prefs = Prefs(ctx)
    private val exec = Executors.newSingleThreadExecutor()
    private val queue = ArrayDeque<String>()
    private var lastPollMs = 0L
    private var retryAfterMs = 0L

    @Volatile var lastResult = "not run yet"
        private set

    val enabled get() = prefs.telegramEnabled

    /** Queue a one-off alert; sent as soon as possible. */
    fun event(text: String) {
        if (!enabled) return
        synchronized(queue) {
            if (queue.size >= MAX_QUEUE) queue.removeFirst()
            queue.addLast(text)
        }
        exec.execute { drain() }
    }

    /**
     * Called every ~15 s by the logger. statusText builds the "/status" reply; currentSession is
     * the live session, which is still growing and goes out with the next report.
     */
    fun tick(statusText: () -> String, latestLine: String, currentSession: Int) {
        if (!enabled) return
        exec.execute {
            drain()
            maybeDailyReport(latestLine, currentSession)
            val now = System.currentTimeMillis()
            if (now - lastPollMs >= POLL_MS) { lastPollMs = now; pollCommands(statusText) }
        }
    }

    fun sendReportNow(latestLine: String, currentSession: Int, done: (String) -> Unit) {
        exec.execute {
            runReport(force = true, latestLine = latestLine, currentSession = currentSession)
            done(lastResult)
        }
    }

    fun test(done: (String) -> Unit) {
        exec.execute {
            val ok = enabled && sendMessage("DPF Sentinel (phone): test message. Alerts will arrive here.")
            lastResult = if (!enabled) "set a bot token (with a colon) and a chat id first" else if (ok) "test message sent" else "test failed: check the token and chat id"
            done(lastResult)
        }
    }

    fun shutdown() { exec.shutdown() }

    private fun drain() {
        while (true) {
            val msg = synchronized(queue) { queue.peekFirst() } ?: return
            if (!sendMessage(msg)) return   // stays queued, retried on the next tick
            synchronized(queue) { if (queue.peekFirst() === msg) queue.removeFirst() }
        }
    }

    private fun maybeDailyReport(latestLine: String, currentSession: Int) {
        val today = LocalDate.now().toEpochDay()
        if (prefs.tgLastReportDay == today) return
        if (LocalTime.now().isBefore(LocalTime.of(prefs.reportHour, prefs.reportMinute))) return
        if (System.currentTimeMillis() < retryAfterMs) return
        if (!runReport(force = false, latestLine = latestLine, currentSession = currentSession)) retryAfterMs = System.currentTimeMillis() + 10 * 60_000L
    }

    private fun runReport(force: Boolean, latestLine: String, currentSession: Int): Boolean {
        var sent = prefs.tgLastSentSession
        // First report (Telegram just switched on): start from now rather than upload every old log.
        if (sent == 0 && currentSession > 1) { sent = currentSession - 1; prefs.tgLastSentSession = sent }
        val all = SessionFiles.list(ctx).filter {
            val n = SessionFiles.numberOf(it)
            n > sent && n != currentSession && (force || it.length() >= MIN_BYTES)
        }.sortedWith(compareBy<File> { SessionFiles.numberOf(it) }.thenBy { it.name })
        // Only the newest files; older unsent ones are marked as handled, not queued.
        val files = all.takeLast(MAX_FILES)
        if (all.size > files.size) { sent = SessionFiles.numberOf(all[all.size - MAX_FILES - 1]); prefs.tgLastSentSession = sent }

        val date = LocalDateTime.now().format(DateTimeFormatter.ofPattern("EEE dd MMM HH:mm", Locale.UK))
        if (files.isEmpty() && !force) {
            prefs.tgLastReportDay = LocalDate.now().toEpochDay()
            lastResult = "nothing new to send"
            return true
        }
        if (!sendMessage(Messages.dailyReport(date, files.size, files.sumOf { it.length() }, latestLine))) {
            lastResult = "message failed"
            return false
        }
        var lastGood = sent
        for (f in files) {
            if (!sendDocument(f, "${f.name} (${Messages.fmtBytes(f.length())})")) {
                prefs.tgLastSentSession = lastGood
                lastResult = "failed on ${f.name}"
                return false
            }
            val n = SessionFiles.numberOf(f)
            if (n > lastGood) lastGood = n
        }
        prefs.tgLastSentSession = lastGood
        prefs.tgLastReportDay = LocalDate.now().toEpochDay()
        lastResult = "sent ${files.size} file(s) at $date"
        return true
    }

    private fun pollCommands(statusText: () -> String) {
        val body = request("getUpdates?offset=${prefs.tgUpdateOffset}&timeout=0&limit=5", null) ?: return
        runCatching {
            val results = JSONObject(body).optJSONArray("result") ?: return
            for (i in 0 until results.length()) {
                val u = results.getJSONObject(i)
                prefs.tgUpdateOffset = u.getLong("update_id") + 1
                val msg = u.optJSONObject("message") ?: continue
                val chat = msg.optJSONObject("chat")?.optLong("id")?.toString() ?: continue
                if (chat != prefs.tgChatId) continue   // only the configured chat is ever answered
                val text = msg.optString("text").trim()
                if (text.startsWith("/status")) sendMessage(statusText())
            }
        }.onFailure { Log.w(TAG, "getUpdates parse failed", it) }
    }

    private fun sendMessage(text: String): Boolean {
        val form = "chat_id=" + URLEncoder.encode(prefs.tgChatId, "UTF-8") + "&text=" + URLEncoder.encode(text, "UTF-8")
        return request("sendMessage", form) != null
    }

    /** Returns the reply body when Telegram answered ok, else null. */
    private fun request(method: String, form: String?): String? = try {
        val c = URL("https://api.telegram.org/bot${prefs.tgToken}/$method").openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 25_000
        if (form != null) {
            c.requestMethod = "POST"
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            c.outputStream.use { it.write(form.toByteArray()) }
        }
        val ok = c.responseCode == 200
        val body = (if (ok) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText() } ?: ""
        c.disconnect()
        if (ok && body.contains("\"ok\":true")) body else null
    } catch (e: Exception) {
        Log.w(TAG, "$method failed: ${e.message}")
        null
    }

    private fun sendDocument(f: File, caption: String): Boolean = try {
        val b = "----dpfsentinel7e3f9a"
        val c = URL("https://api.telegram.org/bot${prefs.tgToken}/sendDocument").openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 60_000
        c.requestMethod = "POST"
        c.doOutput = true
        c.setChunkedStreamingMode(16 * 1024)
        c.setRequestProperty("Content-Type", "multipart/form-data; boundary=$b")
        DataOutputStream(c.outputStream).use { out ->
            fun field(name: String, value: String) =
                out.writeBytes("--$b\r\nContent-Disposition: form-data; name=\"$name\"\r\n\r\n$value\r\n")
            field("chat_id", prefs.tgChatId)
            out.write("--$b\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n$caption\r\n".toByteArray(Charsets.UTF_8))
            out.writeBytes("--$b\r\nContent-Disposition: form-data; name=\"document\"; filename=\"${f.name}\"\r\n")
            out.writeBytes("Content-Type: application/octet-stream\r\n\r\n")
            f.inputStream().use { it.copyTo(out) }
            out.writeBytes("\r\n--$b--\r\n")
        }
        val ok = c.responseCode == 200
        c.disconnect()
        ok
    } catch (e: Exception) {
        Log.w(TAG, "sendDocument failed: ${e.message}")
        false
    }

    companion object {
        private const val TAG = "Telegram"
        private const val MAX_QUEUE = 10
        private const val MAX_FILES = 10
        private const val MIN_BYTES = 3000L   // skip blank / no-adapter sessions
        private const val POLL_MS = 60_000L
    }
}
