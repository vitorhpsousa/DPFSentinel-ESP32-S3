package io.github.vitorhpsousa.dpfsentinel

import android.annotation.SuppressLint
import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import androidx.car.app.notification.CarAppExtender
import androidx.car.app.notification.CarNotificationManager
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import io.github.vitorhpsousa.dpfsentinel.core.HealthState
import io.github.vitorhpsousa.dpfsentinel.core.HealthTracker
import io.github.vitorhpsousa.dpfsentinel.core.Messages
import io.github.vitorhpsousa.dpfsentinel.core.ObdPoller
import io.github.vitorhpsousa.dpfsentinel.core.RegenEvent
import io.github.vitorhpsousa.dpfsentinel.core.RegenWatch
import io.github.vitorhpsousa.dpfsentinel.core.Row
import io.github.vitorhpsousa.dpfsentinel.core.SootZone
import io.github.vitorhpsousa.dpfsentinel.obd.BleElm
import io.github.vitorhpsousa.dpfsentinel.ui.MainActivity
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.atomic.AtomicBoolean

// Reads the car while the phone is locked: the phone's version of the firmware's main loop. One
// thread serves the most overdue OBD request, assembles a row a second, writes the CSV, and feeds
// the regen watch, health tracker, notifications, Telegram and LiveState (phone + car screens).
class LoggerService : Service() {
    @Volatile private var running = false
    private var worker: Thread? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private lateinit var prefs: Prefs
    private lateinit var telegram: Telegram

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        prefs = Prefs(this)
        telegram = (application as DpfApp).telegram
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0
        try {
            ServiceCompat.startForeground(this, NOTIF_LIVE, liveNotification("Starting…"), type)
        } catch (e: Exception) {
            // Missing Bluetooth permission or a background start Android refused.
            Log.w(TAG, "startForeground refused", e)
            stopSelf()
            return START_NOT_STICKY
        }
        if (!running) {
            running = true
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DpfSentinel:logger").apply { acquire(12 * 60 * 60 * 1000L) }
            worker = Thread({ loop() }, "obd-logger").also { it.start() }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        worker?.interrupt()
        worker?.join(3000)
        worker = null
        wakeLock?.let { if (it.isHeld) it.release() }
        LiveState.update { it.copy(running = false, connected = false, stale = true, status = "Not logging") }
        super.onDestroy()
    }

    private fun loop() {
        val elm = BleElm(this)
        val session = SessionFiles(this, prefs.nextSessionNumber())
        val t0 = SystemClock.elapsedRealtime()
        fun ms() = SystemClock.elapsedRealtime() - t0
        fun unix() = System.currentTimeMillis() / 1000.0

        val poller = ObdPoller(elm) { h, r, reply -> session.raw(ms(), h, r, reply) }
        val watch = RegenWatch()
        val health = HealthTracker(HealthState.decode(prefs.health))
        val zone = SootZone()
        val history = ArrayDeque<HistoryPoint>()
        var blankRows = 0
        var deadRawSkip = 0L
        var lastRow = 0L
        var lastReconnect = -RECONNECT_MS
        var lastTelegram = 0L
        var lastNotif = 0L
        var connectFails = 0
        var lastEvent = ""
        var latest: Row? = null
        var status = "Looking for the OBD adapter…"

        fun publish(row: Row?) {
            val h = health.state
            val now = unix()
            LiveState.set(
                Snapshot(
                    running = true,
                    status = status,
                    connected = elm.connected(),
                    adapterName = elm.name,
                    row = row,
                    stale = !elm.connected() || row == null || !row.anyPid(),
                    regenActive = watch.active || (row?.regenActive ?: false),
                    regenStartedAtMs = if (watch.active) t0 + watch.startedAtMs else 0,
                    estimate = Messages.estimate(h),
                    estMiles = h.estMilesToRegen,
                    estNotSoon = h.estNotSoon,
                    estFromLiveRate = h.estFromLiveRate,
                    triggerG = health.regenTriggerG(),
                    rateGPerMi = h.estRateGPerMi,
                    sootZone = zone.zone,
                    history = history.toList(),
                    health = HealthSummary(
                        regensToday = health.regensToday(now),
                        regensThisWeek = health.regensThisWeek(now),
                        avgIntervalMi = health.avgRegenIntervalMi(),
                        regensSinceOil = h.regensSinceOil,
                        oilChangeOdoMi = h.oilChangeOdometerMi,
                        lastWarmupMin = h.lastWarmupMinutes,
                        warmupMedianMin = health.warmupMedianMin(),
                        batteryV = h.lastBatteryV,
                    ),
                    session = session.number,
                    rows = session.rows,
                    lastEvent = lastEvent,
                    idleBlockage = row?.idleBlockageFlagged() ?: false,
                ),
            )
        }

        fun connectAdapter(): Boolean {
            if (!elm.bluetoothOn) { status = "Bluetooth is off"; return false }
            // A saved adapter is reconnected by address; every third failure scans instead, so a
            // replaced adapter is found without a trip to the settings.
            val saved = prefs.adapterAddress
            val useSaved = saved.isNotEmpty() && connectFails % 3 != 2
            status = if (useSaved) "Connecting to ${prefs.adapterName.ifEmpty { saved }}…" else "Scanning for the OBD adapter…"
            publish(latest)
            val dev = elm.find(if (useSaved) saved else "")
            if (dev == null) {
                connectFails++
                status = "No adapter found: plug it in, ignition on, and close other OBD apps"
                return false
            }
            if (!elm.connect(dev) || !poller.initAdapter(ms())) {
                elm.close()
                connectFails++
                status = "Adapter found but it didn't answer, retrying"
                return false
            }
            connectFails = 0
            prefs.adapterAddress = dev.address
            prefs.adapterName = elm.name
            status = "Connected to ${elm.name}"
            return true
        }

        try {
            publish(null)
            while (running) {
                var now = ms()
                if (!elm.connected()) {
                    if (now - lastReconnect >= RECONNECT_MS) {
                        // A blank row marks the gap in the CSV, as the firmware does.
                        session.writeRow(now, unix(), Row().values)
                        latest = null
                        connectAdapter()
                        lastReconnect = ms()
                        publish(latest)
                    }
                    Thread.sleep(200)
                    continue
                }

                // While nothing decodes (ECU asleep, adapter still powered) keep the first few
                // cycles of raw replies and then one in sixty, so a parked car doesn't fill the phone.
                val keepRaw = blankRows < 5 || (++deadRawSkip % 60) == 0L
                if (!poller.step(now, keepRaw)) Thread.sleep(5)

                now = ms()
                if (now - lastRow < ROW_MS) continue
                lastRow = now
                val row = poller.assembleRow(now)
                latest = row
                val wall = unix()
                session.writeRow(now, wall, row.values)
                session.flushRaw()

                val ev = watch.update(now, wall, row["dpf_regen_active"], row.egt, row.soot, row.sinceRegen, row.diffPressure)
                if (ev.type != RegenEvent.Type.NONE) {
                    lastEvent = ev.text
                    // Still-running pings update the screens only; start/finish/interrupted alert.
                    if (ev.type != RegenEvent.Type.TICK) {
                        alert(ev.text.substringBefore('\n'), ev.text, if (ev.type == RegenEvent.Type.START) NOTIF_REGEN_START else NOTIF_REGEN_END)
                        telegram.event(ev.text)
                    }
                }
                if (oilChangeRequested.getAndSet(false) && !row.odometer.isNaN()) health.oilChangeNow(row.odometer)
                health.update(row, ev, now, wall)
                for (a in health.takeAlerts()) {
                    alert("DPF Sentinel health", a.text, NOTIF_HEALTH + a.ordinal)
                    telegram.event(a.text)
                }
                if (health.dirty) { prefs.health = health.state.encode(); health.dirty = false }

                zone.update(row.soot)?.let { z ->
                    val text = if (z == 2) "Soot ${"%.1f".format(row.soot)} g: at this car's regen level. A steady 20+ min drive at 50+ mph helps it finish."
                    else "Soot ${"%.1f".format(row.soot)} g: rising towards a regeneration."
                    alert(if (z == 2) "Soot high" else "Soot rising", text, NOTIF_SOOT)
                }

                blankRows = if (row.anyPid()) 0 else blankRows + 1
                status = when {
                    row.anyPid() -> "Connected to ${elm.name}"
                    else -> "Connected to ${elm.name}, waiting for the engine"
                }
                if (blankRows == BLANK_ROWS_REINIT) {
                    poller.initAdapter(ms())
                } else if (blankRows >= BLANK_ROWS_RECONNECT) {
                    blankRows = 0
                    elm.close()
                }

                history.addLast(HistoryPoint(now, row.soot, row.diffPressure, row.egt, row.rpm))
                while (history.isNotEmpty() && now - history.first().ms > HISTORY_MS) history.removeFirst()
                publish(row)

                if (now - lastTelegram >= 15_000) {
                    lastTelegram = now
                    val snapRow = row
                    val est = Messages.estimate(health.state)
                    telegram.tick(
                        statusText = { Messages.status(LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm")), snapRow, est) },
                        latestLine = Messages.latestLine(snapRow),
                        currentSession = session.number,
                    )
                }
                if (now - lastNotif >= 5000) {
                    lastNotif = now
                    updateLiveNotification(row, Messages.estimate(health.state))
                }
            }
        } catch (e: InterruptedException) {
            // stopping
        } catch (e: Exception) {
            Log.e(TAG, "logger loop crashed", e)
        } finally {
            elm.close()
            session.close()
            if (health.dirty) prefs.health = health.state.encode()
        }
    }

    private fun openApp(): PendingIntent = PendingIntent.getActivity(
        this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )

    private fun liveNotification(text: String): Notification =
        NotificationCompat.Builder(this, DpfApp.CH_LIVE)
            .setSmallIcon(R.drawable.ic_dot)
            .setContentTitle("DPF Sentinel is logging")
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(openApp())
            .build()

    @SuppressLint("MissingPermission")
    private fun updateLiveNotification(row: Row, estimate: String) {
        val text = if (row.soot.isNaN()) LiveState.state.value.status
        else "Soot ${"%.1f".format(row.soot)} g · next regen $estimate" + if (LiveState.state.value.regenActive) " · REGEN RUNNING" else ""
        NotificationManagerCompat.from(this).notify(NOTIF_LIVE, liveNotification(text))
    }

    // Posted to the phone and, through CarAppExtender, to the Android Auto screen.
    @SuppressLint("MissingPermission")
    private fun alert(title: String, text: String, id: Int) {
        val b = NotificationCompat.Builder(this, DpfApp.CH_ALERTS)
            .setSmallIcon(R.drawable.ic_dot)
            .setContentTitle(title)
            .setContentText(text.substringAfter('\n', text))
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .setContentIntent(openApp())
            .extend(
                CarAppExtender.Builder()
                    .setContentTitle(title)
                    .setContentText(text.substringAfter('\n', text).substringBefore('\n'))
                    .setSmallIcon(R.drawable.ic_dot)
                    .setImportance(NotificationManagerCompat.IMPORTANCE_HIGH)
                    .build(),
            )
        runCatching { CarNotificationManager.from(this).notify(id, b) }
            .onFailure { Log.w(TAG, "notify failed", it) }
    }

    companion object {
        private const val TAG = "LoggerService"
        private const val NOTIF_LIVE = 1
        private const val NOTIF_REGEN_START = 10
        private const val NOTIF_REGEN_END = 11
        private const val NOTIF_SOOT = 12
        private const val NOTIF_HEALTH = 20
        private const val ROW_MS = 1000L
        private const val RECONNECT_MS = 5000L
        private const val HISTORY_MS = 120_000L
        private const val BLANK_ROWS_REINIT = 10
        private const val BLANK_ROWS_RECONNECT = 30

        /** Set by the "Oil changed" button; applied by the logger on its next row. */
        val oilChangeRequested = AtomicBoolean(false)

        /** Throws if Android refuses a foreground-service start (e.g. from the background). */
        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, LoggerService::class.java))
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, LoggerService::class.java))
        }
    }
}
