package io.github.vitorhpsousa.dpfsentinel.ui

import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.vitorhpsousa.dpfsentinel.HistoryPoint
import io.github.vitorhpsousa.dpfsentinel.LoggerService
import io.github.vitorhpsousa.dpfsentinel.Prefs
import io.github.vitorhpsousa.dpfsentinel.Snapshot
import java.util.Locale

val Green = Color(0xFF43A047)
val Amber = Color(0xFFFFB300)
val Red = Color(0xFFE53935)
val Muted = Color(0xFF9AA0A6)

fun fmt(v: Double, d: Int): String = if (v.isNaN()) "--" else String.format(Locale.getDefault(), "%.${d}f", v)

fun zoneColor(z: Int) = when (z) { 2 -> Red; 1 -> Amber; else -> Green }

@Composable
fun LiveTab(s: Snapshot, onStart: () -> Unit, onStop: () -> Unit) {
    val ctx = LocalContext.current
    val r = s.row
    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(if (s.running) s.status else "Not logging", color = if (s.connected) Green else Muted)
                if (s.running) Text("session_${s.session}.csv · ${s.rows} rows", color = Muted, fontSize = 12.sp)
            }
            if (s.running) OutlinedButton(onClick = onStop) { Text("Stop") }
            else Button(onClick = onStart, colors = ButtonDefaults.buttonColors(containerColor = Green)) { Text("Start logging") }
        }

        if (s.regenActive) {
            val mins = if (s.regenStartedAtMs > 0) (SystemClock.elapsedRealtime() - s.regenStartedAtMs) / 60000 else 0
            Banner("REGENERATING · $mins min · EGT ${fmt(r?.egt ?: Double.NaN, 0)} °C", "Keep driving steadily so it can finish.", Amber)
        }
        if (s.idleBlockage) Banner("DPF pressure high at idle", "A possible blockage sign (threshold unverified).", Red)

        // Soot: the number this whole project is about.
        val soot = r?.soot ?: Double.NaN
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF15181C))) {
            Column(Modifier.padding(16.dp).fillMaxWidth()) {
                Text("Soot", color = Muted)
                Text(
                    if (soot.isNaN()) "--" else "${fmt(soot, 1)} g",
                    fontSize = 56.sp, fontWeight = FontWeight.Bold,
                    color = if (soot.isNaN() || s.stale) Muted else zoneColor(s.sootZone),
                )
                Text("Next regen: ${s.estimate}", fontSize = 20.sp)
                Text(
                    buildString {
                        if (!s.triggerG.isNaN()) append("regen trigger ~${fmt(s.triggerG, 1)} g")
                        if (!s.rateGPerMi.isNaN()) append(" · ${fmt(s.rateGPerMi, 3)} g/mi")
                        if (s.estNotSoon) append(" · estimate shows from 12 g")
                    },
                    color = Muted, fontSize = 12.sp,
                )
            }
        }

        if (s.history.size > 2) {
            Chart("Soot (g), last 2 min", s.history, Amber) { it.soot }
            Chart("DPF ΔP (hPa)", s.history, Color(0xFF4FC3F7)) { it.diffPressure }
        }

        val offset = Prefs(ctx).odometerOffsetMi
        val tiles = listOf(
            "RPM" to fmt(r?.rpm ?: Double.NaN, 0),
            "Speed" to fmt(r?.speed ?: Double.NaN, 0) + " km/h",
            "Coolant" to fmt(r?.coolant ?: Double.NaN, 0) + " °C",
            "DPF ΔP" to fmt(r?.diffPressure ?: Double.NaN, 1) + " hPa",
            "EGT before DPF" to fmt(r?.egt ?: Double.NaN, 0) + " °C",
            "Cat temp" to fmt(r?.catTemp ?: Double.NaN, 0) + " °C",
            "Since regen" to fmt(r?.sinceRegen ?: Double.NaN, 1) + " mi",
            "Odometer" to fmt((r?.odometer ?: Double.NaN) + offset, 0) + " mi",
            "Battery" to fmt(r?.batteryV ?: Double.NaN, 1) + " V",
            "Intercooler" to fmt(r?.intercooler ?: Double.NaN, 0) + " °C",
            "MAF" to fmt(r?.maf ?: Double.NaN, 1) + " g/s",
            "Load" to fmt(r?.get("engine_load_pct") ?: Double.NaN, 0) + " %",
        )
        tiles.chunked(3).forEach { line ->
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                line.forEach { (k, v) -> Tile(k, v, Modifier.weight(1f)) }
            }
        }

        val h = s.health
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF15181C))) {
            Column(Modifier.padding(16.dp).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Health", fontWeight = FontWeight.Bold)
                Text("Regens today ${h.regensToday} · this week ${h.regensThisWeek} · avg interval ${fmt(h.avgIntervalMi, 0)} mi")
                Text("Regens since oil change: ${h.regensSinceOil}" + if (h.oilChangeOdoMi > 0) " (reference ${fmt(h.oilChangeOdoMi + offset, 0)} mi)" else "")
                Text("Warm-up to 80 °C: last ${fmt(h.lastWarmupMin, 1)} min · usual ${fmt(h.warmupMedianMin, 1)} min")
                Spacer(Modifier.height(4.dp))
                OutlinedButton(
                    enabled = s.running && !(r?.odometer ?: Double.NaN).isNaN(),
                    onClick = { LoggerService.oilChangeRequested.set(true) },
                ) { Text("Oil changed today") }
            }
        }
        if (s.lastEvent.isNotEmpty()) Text("Last event: ${s.lastEvent}", color = Muted, fontSize = 12.sp)
        Text(
            "Read-only logger, not a diagnostic tool. Don't base safety or repair decisions on it.",
            color = Muted, fontSize = 11.sp,
        )
    }
}

@Composable
private fun Banner(title: String, text: String, color: Color) {
    Column(Modifier.fillMaxWidth().background(color.copy(alpha = 0.2f), RoundedCornerShape(12.dp)).padding(12.dp)) {
        Text(title, color = color, fontWeight = FontWeight.Bold)
        Text(text)
    }
}

@Composable
private fun Tile(label: String, value: String, modifier: Modifier) {
    Column(modifier.background(Color(0xFF15181C), RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(label, color = Muted, fontSize = 11.sp)
        Text(value, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun Chart(title: String, pts: List<HistoryPoint>, color: Color, value: (HistoryPoint) -> Double) {
    val vals = pts.map(value)
    val good = vals.filter { !it.isNaN() }
    Column {
        Text(
            "$title  " + if (good.isEmpty()) "" else "${fmt(good.min(), 1)} – ${fmt(good.max(), 1)}",
            color = Muted, fontSize = 12.sp,
        )
        Canvas(Modifier.fillMaxWidth().height(70.dp).background(Color(0xFF15181C), RoundedCornerShape(8.dp))) {
            if (good.size < 2) return@Canvas
            val lo = good.min()
            val hi = good.max().let { if (it - lo < 1e-6) lo + 1 else it }
            val t0 = pts.first().ms
            val span = (pts.last().ms - t0).coerceAtLeast(1)
            val path = Path()
            var started = false
            pts.forEachIndexed { i, p ->
                val v = vals[i]
                if (v.isNaN()) { started = false; return@forEachIndexed }
                val o = Offset(
                    size.width * (p.ms - t0) / span,
                    size.height - 6f - (size.height - 12f) * ((v - lo) / (hi - lo)).toFloat(),
                )
                if (started) path.lineTo(o.x, o.y) else path.moveTo(o.x, o.y)
                started = true
            }
            drawPath(path, color, style = Stroke(width = 3f))
        }
    }
}
