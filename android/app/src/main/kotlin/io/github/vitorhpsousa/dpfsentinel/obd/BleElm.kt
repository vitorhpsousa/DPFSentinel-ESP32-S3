package io.github.vitorhpsousa.dpfsentinel.obd

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.util.Log
import io.github.vitorhpsousa.dpfsentinel.core.ElmTransport
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

// BLE GATT link to an ELM327 adapter that exposes a UART bridge (a notify characteristic for
// replies, a writable one for commands): the Android side of src/obd/elm_client.cpp. Commands end
// in '\r', replies end in '>'. All calls block and are made from the logger thread only.
@SuppressLint("MissingPermission")   // the activity asks for the Bluetooth permissions before the service starts
class BleElm(private val context: Context) : ElmTransport {
    private val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter

    private var gatt: BluetoothGatt? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var chunk = 20   // ATT payload per write; raised after the MTU exchange

    private val rxBuf = StringBuilder()
    private val rxLock = Object()
    @Volatile private var linkUp = false

    // GATT callback results handed from the binder thread to the logger thread.
    private val connQ = LinkedBlockingQueue<Boolean>()
    private val opQ = LinkedBlockingQueue<Int>()

    var device: BluetoothDevice? = null
        private set
    val name: String get() = device?.let { it.name ?: it.address } ?: ""

    val bluetoothOn: Boolean get() = adapter?.isEnabled == true

    fun connected(): Boolean = linkUp && tx != null && rx != null

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            val up = status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED
            if (!up) linkUp = false
            connQ.offer(up)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) { opQ.offer(status) }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) chunk = (mtu - 3).coerceIn(20, 244)
            opQ.offer(status)
        }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) { opQ.offer(status) }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { opQ.offer(status) }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = append(value)

        @Deprecated("Used before Android 13")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) c.value?.let { append(it) }
        }
    }

    private fun append(bytes: ByteArray) {
        synchronized(rxLock) {
            if (rxBuf.length < 4096) rxBuf.append(String(bytes, Charsets.ISO_8859_1))
            rxLock.notifyAll()
        }
    }

    private fun looksLikeAdapter(name: String?): Boolean {
        if (name.isNullOrEmpty()) return false
        val n = name.uppercase()
        return NAME_HINTS.any { n.contains(it.uppercase()) }
    }

    /**
     * Finds the adapter: the saved address if there is one, else a scan that takes the strongest
     * adapter-like name in range (several may be: a Konnwei and an OBDLink).
     */
    fun find(savedAddress: String, scanMs: Long = 8000): BluetoothDevice? {
        val a = adapter ?: return null
        if (!a.isEnabled) return null
        if (savedAddress.isNotEmpty()) return runCatching { a.getRemoteDevice(savedAddress) }.getOrNull()
        val scanner = a.bluetoothLeScanner ?: return null
        var best: ScanResult? = null
        val lock = Object()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val nm = result.scanRecord?.deviceName ?: result.device.name
                if (!looksLikeAdapter(nm)) return
                synchronized(lock) { if (best == null || result.rssi > best!!.rssi) best = result }
            }
        }
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(null, settings, cb)
        try { Thread.sleep(scanMs) } catch (e: InterruptedException) { Thread.currentThread().interrupt() }
        runCatching { scanner.stopScan(cb) }
        return synchronized(lock) { best?.device }
    }

    /** Connects, discovers the UART characteristics and subscribes to replies. */
    fun connect(dev: BluetoothDevice): Boolean {
        close()
        device = dev
        connQ.clear(); opQ.clear()
        val g = dev.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE) ?: return false
        gatt = g
        if (connQ.poll(12, TimeUnit.SECONDS) != true) { Log.w(TAG, "connect timed out"); close(); return false }
        linkUp = true
        if (g.requestMtu(247)) opQ.poll(3, TimeUnit.SECONDS)
        if (!g.discoverServices() || opQ.poll(10, TimeUnit.SECONDS) != BluetoothGatt.GATT_SUCCESS) {
            Log.w(TAG, "service discovery failed"); close(); return false
        }
        if (!pickCharacteristics(g)) { Log.w(TAG, "no notify + write characteristic pair"); close(); return false }
        if (!subscribe(g, rx!!)) { Log.w(TAG, "subscribe failed"); close(); return false }
        Log.i(TAG, "using ${tx!!.service.uuid} TX ${tx!!.uuid} RX ${rx!!.uuid}")
        return true
    }

    private fun canWrite(c: BluetoothGattCharacteristic) =
        c.properties and (BluetoothGattCharacteristic.PROPERTY_WRITE or BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0

    private fun canNotify(c: BluetoothGattCharacteristic) =
        c.properties and (BluetoothGattCharacteristic.PROPERTY_NOTIFY or BluetoothGattCharacteristic.PROPERTY_INDICATE) != 0

    // Prefer Nordic UART; otherwise the first non-generic service with a notify and a writable
    // characteristic (HM-10 style adapters use one characteristic for both).
    private fun pickCharacteristics(g: BluetoothGatt): Boolean {
        tx = null; rx = null
        val services = g.services.filter {
            val u = it.uuid.toString().lowercase()
            !(u.endsWith(BASE_UUID_SUFFIX) && u.substring(4, 8) in GENERIC_SERVICES)
        }
        val ordered = services.sortedBy { if (it.uuid == NUS) 0 else 1 }
        for (svc in ordered) {
            val chars = svc.characteristics
            val r = chars.firstOrNull { canNotify(it) } ?: continue
            val t = chars.firstOrNull { it != r && canWrite(it) } ?: r.takeIf { canWrite(it) } ?: continue
            rx = r; tx = t
            return true
        }
        return false
    }

    @Suppress("DEPRECATION")   // the pre-Android 13 descriptor write
    private fun subscribe(g: BluetoothGatt, c: BluetoothGattCharacteristic): Boolean {
        if (!g.setCharacteristicNotification(c, true)) return false
        val d = c.getDescriptor(CCCD) ?: return true   // some clones notify without a CCCD
        val value = if (c.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0)
            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE else BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(d, value) == BluetoothGatt.GATT_SUCCESS
        } else {
            d.value = value
            g.writeDescriptor(d)
        }
        return started && opQ.poll(5, TimeUnit.SECONDS) == BluetoothGatt.GATT_SUCCESS
    }

    @Suppress("DEPRECATION")   // the pre-Android 13 characteristic write
    private fun write(bytes: ByteArray): Boolean {
        val g = gatt ?: return false
        val c = tx ?: return false
        val noResponse = c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0
        val type = if (noResponse) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        var off = 0
        while (off < bytes.size) {
            val part = bytes.copyOfRange(off, minOf(bytes.size, off + chunk))
            opQ.clear()
            val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeCharacteristic(c, part, type) == BluetoothGatt.GATT_SUCCESS
            } else {
                c.writeType = type
                c.value = part
                g.writeCharacteristic(c)
            }
            if (!started) return false
            // Android reports onCharacteristicWrite for both write types; wait so writes don't overlap.
            opQ.poll(2, TimeUnit.SECONDS)
            off += part.size
        }
        return true
    }

    override fun send(cmd: String, timeoutMs: Long): String {
        if (!connected()) return ""
        synchronized(rxLock) { rxBuf.setLength(0) }
        if (!write((cmd + "\r").toByteArray(Charsets.ISO_8859_1))) return ""
        val deadline = System.currentTimeMillis() + timeoutMs
        synchronized(rxLock) {
            while (true) {
                val gt = rxBuf.indexOf(">")
                if (gt >= 0) return rxBuf.substring(0, gt).trim()
                val left = deadline - System.currentTimeMillis()
                if (left <= 0 || !linkUp) return rxBuf.toString().trim()
                rxLock.wait(left)
            }
        }
    }

    fun close() {
        linkUp = false
        gatt?.let { g -> runCatching { g.disconnect() }; runCatching { g.close() } }
        gatt = null
        tx = null
        rx = null
        chunk = 20
    }

    companion object {
        private const val TAG = "BleElm"
        private val NUS: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val BASE_UUID_SUFFIX = "-0000-1000-8000-00805f9b34fb"
        private val GENERIC_SERVICES = setOf("1800", "1801", "180a")
        val NAME_HINTS = listOf(
            "OBD", "ELM", "VLINK", "V-LINK", "VEEPEAK", "IOS-", "VGATE", "KONNWEI", "KW9", "KW8", "KW-", "OBDLINK", "MX+", "LELink", "CX",
        )
    }
}
