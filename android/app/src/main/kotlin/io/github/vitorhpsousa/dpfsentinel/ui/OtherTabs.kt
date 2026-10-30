package io.github.vitorhpsousa.dpfsentinel.ui

import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import io.github.vitorhpsousa.dpfsentinel.DpfApp
import io.github.vitorhpsousa.dpfsentinel.Prefs
import io.github.vitorhpsousa.dpfsentinel.SessionFiles
import io.github.vitorhpsousa.dpfsentinel.Snapshot
import io.github.vitorhpsousa.dpfsentinel.core.Messages
import java.io.File
import java.text.DateFormat
import java.util.Date

@Composable
fun LogsTab() {
    val ctx = LocalContext.current
    var refresh by remember { mutableIntStateOf(0) }
    var confirmDelete by remember { mutableStateOf<File?>(null) }
    val files = remember(refresh) { SessionFiles.list(ctx) }

    fun share(f: File) {
        val uri = FileProvider.getUriForFile(ctx, ctx.packageName + ".files", f)
        val send = Intent(Intent.ACTION_SEND).apply {
            type = if (f.name.endsWith(".csv")) "text/csv" else "text/plain"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        ctx.startActivity(Intent.createChooser(send, f.name))
    }

    Column(Modifier.padding(16.dp)) {
        Text("Logs", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Text(
            "Same files as the board's SD card, so tools/*.py work on them. Also in Android/data/${ctx.packageName}/files/obd.",
            color = Muted, fontSize = 12.sp,
        )
        if (files.isEmpty()) Text("No logs yet. Start logging on the Live tab.", Modifier.padding(top = 16.dp))
        LazyColumn {
            items(files, key = { it.name }) { f ->
                Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(f.name)
                        Text(
                            Messages.fmtBytes(f.length()) + " · " + DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(f.lastModified())),
                            color = Muted, fontSize = 12.sp,
                        )
                    }
                    TextButton(onClick = { share(f) }) { Text("Share") }
                    TextButton(onClick = { confirmDelete = f }) { Text("Delete") }
                }
                HorizontalDivider()
            }
        }
    }

    confirmDelete?.let { f ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text("Delete ${f.name}?") },
            confirmButton = { TextButton(onClick = { f.delete(); confirmDelete = null; refresh++ }) { Text("Delete") } },
            dismissButton = { TextButton(onClick = { confirmDelete = null }) { Text("Cancel") } },
        )
    }
}

@Composable
fun SettingsTab(s: Snapshot) {
    val ctx = LocalContext.current
    val prefs = remember { Prefs(ctx) }
    val telegram = (ctx.applicationContext as DpfApp).telegram
    var token by remember { mutableStateOf(prefs.tgToken) }
    var chat by remember { mutableStateOf(prefs.tgChatId) }
    var reportTime by remember { mutableStateOf("%02d:%02d".format(prefs.reportHour, prefs.reportMinute)) }
    var tgResult by remember { mutableStateOf(telegram.lastResult) }
    var dash by remember { mutableStateOf("") }
    var autoCar by remember { mutableStateOf(prefs.autoStartWithCar) }
    var adapter by remember { mutableStateOf(prefs.adapterName.ifEmpty { prefs.adapterAddress }) }

    fun toast(t: String) = Toast.makeText(ctx, t, Toast.LENGTH_LONG).show()
    fun onMain(f: () -> Unit) = android.os.Handler(android.os.Looper.getMainLooper()).post(f)

    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Telegram", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Text(
            "Regen start/finish and health alerts, a daily report with the new logs, and /status replies. " +
                "Make a bot with @BotFather and get your chat id (docs/telegram-setup.md).",
            color = Muted, fontSize = 12.sp,
        )
        OutlinedTextField(token, { token = it }, label = { Text("Bot token (12345678:AA...)") }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
        OutlinedTextField(chat, { chat = it }, label = { Text("Chat id") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
        OutlinedTextField(reportTime, { reportTime = it }, label = { Text("Daily report time (HH:MM)") }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                if (token.isNotBlank() && !token.contains(':')) { toast("The token must look like 12345678:AAbb... (bot id, colon, secret)"); return@Button }
                prefs.tgToken = token
                prefs.tgChatId = chat
                val hm = Regex("""^(\d{1,2}):(\d{2})$""").find(reportTime.trim())
                if (hm != null && hm.groupValues[1].toInt() < 24 && hm.groupValues[2].toInt() < 60) {
                    prefs.reportHour = hm.groupValues[1].toInt()
                    prefs.reportMinute = hm.groupValues[2].toInt()
                }
                toast("Saved")
            }) { Text("Save") }
            OutlinedButton(onClick = { telegram.test { r -> onMain { tgResult = r } } }) { Text("Send test") }
            OutlinedButton(onClick = {
                telegram.sendReportNow(Messages.latestLine(s.row), if (s.running) s.session else -1) { r -> onMain { tgResult = r } }
            }) { Text("Report now") }
        }
        Text("Last result: $tgResult", color = Muted, fontSize = 12.sp)

        HorizontalDivider()
        Text("Android Auto", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Start logging when Android Auto opens DPF Sentinel", Modifier.weight(1f))
            Switch(autoCar, { autoCar = it; prefs.autoStartWithCar = it })
        }
        Text(
            "Sideloaded apps only show in the car after enabling Android Auto's developer mode and \"Unknown sources\". See android/README.md.",
            color = Muted, fontSize = 12.sp,
        )

        HorizontalDivider()
        Text("OBD adapter", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Text(if (adapter.isEmpty()) "None saved: the first BLE adapter found (OBD/ELM/Vlink/Konnwei… names) is used and remembered." else "Saved: $adapter")
        OutlinedButton(enabled = adapter.isNotEmpty(), onClick = {
            prefs.adapterAddress = ""
            prefs.adapterName = ""
            adapter = ""
            toast("Forgotten: the next connection scans again")
        }) { Text("Forget adapter") }

        HorizontalDivider()
        Text("Dash mileage", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        val ecu = s.row?.odometer ?: Double.NaN
        Text(
            "The dash and the ECU often count separately. With the engine running, type the dash mileage once; " +
                "the offset is used for display only, never written to the logs. Current offset: ${fmt(prefs.odometerOffsetMi.toDouble(), 1)} mi.",
            color = Muted, fontSize = 12.sp,
        )
        OutlinedTextField(dash, { dash = it }, label = { Text("Dash mileage now (mi)") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = !ecu.isNaN(), onClick = {
                val d = dash.replace(',', '.').toDoubleOrNull()
                if (d == null) toast("Enter a number") else {
                    prefs.odometerOffsetMi = (d - ecu).toFloat()
                    toast("Offset set to ${fmt(d - ecu, 1)} mi")
                }
            }) { Text(if (ecu.isNaN()) "Needs a live reading" else "Set offset") }
            OutlinedButton(onClick = { prefs.odometerOffsetMi = 0f; toast("Offset cleared") }) { Text("Clear") }
        }

        HorizontalDivider()
        Text(
            "DPF Sentinel is a read-only logger for a 2014 Hyundai ix35 1.7 CRDi; other cars use other PIDs (docs/other-cars.md). " +
                "Not a diagnostic tool. ${Messages.KOFI_URL}",
            color = Muted, fontSize = 11.sp,
        )
    }
}
