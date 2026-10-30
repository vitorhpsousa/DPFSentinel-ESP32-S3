package io.github.vitorhpsousa.dpfsentinel.car

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.SystemClock
import androidx.car.app.CarContext
import androidx.car.app.CarToast
import androidx.car.app.Screen
import androidx.car.app.constraints.ConstraintManager
import androidx.car.app.model.Action
import androidx.car.app.model.CarColor
import androidx.car.app.model.CarIcon
import androidx.car.app.model.Pane
import androidx.car.app.model.PaneTemplate
import androidx.car.app.model.Row
import androidx.car.app.model.Template
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import io.github.vitorhpsousa.dpfsentinel.LiveState
import io.github.vitorhpsousa.dpfsentinel.LoggerService
import io.github.vitorhpsousa.dpfsentinel.R
import io.github.vitorhpsousa.dpfsentinel.Snapshot
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale

// The Android Auto screen: soot, next regen, temperatures and link status on a PaneTemplate.
// Hosts limit how often a template may change, so it is re-sent at most every 2 s and only
// when the text on it changed.
class LiveScreen(carContext: CarContext) : Screen(carContext) {
    private var snap: Snapshot = LiveState.state.value
    private var lastKey = ""
    private var lastEvent = snap.lastEvent

    init {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                while (true) {
                    val s = LiveState.state.value
                    if (s.lastEvent.isNotEmpty() && s.lastEvent != lastEvent) {
                        CarToast.makeText(carContext, s.lastEvent.substringBefore('\n'), CarToast.LENGTH_LONG).show()
                    }
                    lastEvent = s.lastEvent
                    val key = rows(s).joinToString("|") { it.first + "/" + it.second } + s.running + s.sootZone
                    if (key != lastKey) {
                        lastKey = key
                        snap = s
                        invalidate()
                    }
                    delay(REFRESH_MS)
                }
            }
        }
    }

    fun startLogging(quiet: Boolean = false) {
        val granted = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            ContextCompat.checkSelfPermission(carContext, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            CarToast.makeText(carContext, "Open DPF Sentinel on the phone once to allow Bluetooth", CarToast.LENGTH_LONG).show()
            return
        }
        try {
            LoggerService.start(carContext)
        } catch (e: Exception) {
            // Android may refuse to start a foreground service from the car screen alone.
            if (!quiet) CarToast.makeText(carContext, "Start logging from the phone app", CarToast.LENGTH_LONG).show()
        }
    }

    private fun n(v: Double, d: Int) = if (v.isNaN()) "--" else String.format(Locale.US, "%.${d}f", v)

    /** (title, text) per row, most important first. */
    private fun rows(s: Snapshot): List<Pair<String, String>> {
        val r = s.row
        val soot = r?.soot ?: Double.NaN
        val zoneWord = when (s.sootZone) { 2 -> "HIGH"; 1 -> "rising"; else -> "OK" }
        val out = ArrayList<Pair<String, String>>()
        out += Pair(
            if (soot.isNaN()) "Soot --" else "Soot ${n(soot, 1)} g · $zoneWord",
            if (s.triggerG.isNaN()) "" else "This car regenerates at about ${n(s.triggerG, 1)} g",
        )
        if (s.regenActive) {
            val mins = if (s.regenStartedAtMs > 0) (SystemClock.elapsedRealtime() - s.regenStartedAtMs) / 60000 else 0
            out += Pair("REGENERATING · $mins min", "Keep driving steadily so it can finish")
        } else {
            out += Pair("Next regen: ${s.estimate}", if (s.estNotSoon) "Estimate shows from 12 g of soot" else if (s.estFromLiveRate) "From this cycle's soot rate" else "")
        }
        out += Pair(
            "EGT ${n(r?.egt ?: Double.NaN, 0)} °C · DPF ΔP ${n(r?.diffPressure ?: Double.NaN, 0)} hPa",
            "${n(r?.sinceRegen ?: Double.NaN, 0)} mi since the last regen",
        )
        out += Pair(if (s.running) s.status else "Not logging", if (s.running && s.idleBlockage) "High DPF pressure at idle" else "")
        return out
    }

    private fun zoneColor(z: Int): CarColor = when (z) { 2 -> CarColor.RED; 1 -> CarColor.YELLOW; else -> CarColor.GREEN }

    override fun onGetTemplate(): Template {
        val s = snap
        val limit = runCatching {
            carContext.getCarService(ConstraintManager::class.java).getContentLimit(ConstraintManager.CONTENT_LIMIT_TYPE_PANE)
        }.getOrDefault(4)

        val pane = Pane.Builder()
        rows(s).take(limit.coerceAtLeast(1)).forEachIndexed { i, (title, text) ->
            val b = Row.Builder().setTitle(title)
            if (text.isNotEmpty()) b.addText(text)
            if (i == 0 && s.row != null && !s.row.soot.isNaN()) {
                b.setImage(CarIcon.Builder(IconCompat.createWithResource(carContext, R.drawable.ic_dot)).setTint(zoneColor(s.sootZone)).build())
            }
            pane.addRow(b.build())
        }
        pane.addAction(
            if (s.running) {
                Action.Builder().setTitle("Stop logging").setOnClickListener { LoggerService.stop(carContext) }.build()
            } else {
                Action.Builder().setTitle("Start logging").setBackgroundColor(CarColor.GREEN).setOnClickListener { startLogging() }.build()
            },
        )
        return PaneTemplate.Builder(pane.build())
            .setHeaderAction(Action.APP_ICON)
            .setTitle("DPF Sentinel")
            .build()
    }

    companion object {
        private const val REFRESH_MS = 2000L
    }
}
