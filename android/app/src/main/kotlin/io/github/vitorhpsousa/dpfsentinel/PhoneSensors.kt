package io.github.vitorhpsousa.dpfsentinel

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.location.altitude.AltitudeConverter
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * The phone's own GPS and barometer, read on a background thread and sampled once a second by
 * LoggerService. Everything is optional: no permission, no fix or no barometer just leaves NaNs.
 */
class PhoneSensors(private val context: Context) {
    class Sample(
        val lat: Double = Double.NaN,
        val lon: Double = Double.NaN,
        val speedKmh: Double = Double.NaN,
        val altM: Double = Double.NaN,      // above mean sea level when the phone can convert, else WGS84
        val vAccM: Double = Double.NaN,
        val hpa: Double = Double.NaN,
    )

    private val thread = HandlerThread("phone-sensors")
    private val lm = context.getSystemService(LocationManager::class.java)
    private val sm = context.getSystemService(SensorManager::class.java)
    private var converter: Any? = null

    @Volatile private var fix: Location? = null
    @Volatile private var fixAt = 0L
    @Volatile private var hpa = Double.NaN
    @Volatile private var hpaAt = 0L

    val hasLocationPermission: Boolean
        get() = ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    private val locListener = LocationListener { loc ->
        if (Build.VERSION.SDK_INT >= 34 && !loc.hasMslAltitude() && loc.hasAltitude()) {
            // Converts the GPS (ellipsoid) height to sea level; in the UK the two differ by ~50 m.
            runCatching {
                val c = (converter ?: AltitudeConverter().also { converter = it }) as AltitudeConverter
                c.addMslAltitudeToLocation(context, loc)
            }
        }
        fix = loc
        fixAt = SystemClock.elapsedRealtime()
    }

    private val baroListener = object : SensorEventListener {
        override fun onSensorChanged(e: SensorEvent) {
            hpa = e.values[0].toDouble()
            hpaAt = SystemClock.elapsedRealtime()
        }
        override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
    }

    @SuppressLint("MissingPermission")
    fun start() {
        thread.start()
        val handler = Handler(thread.looper)
        sm?.getDefaultSensor(Sensor.TYPE_PRESSURE)?.let {
            sm.registerListener(baroListener, it, SensorManager.SENSOR_DELAY_NORMAL, handler)
        }
        if (hasLocationPermission && lm != null && lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            runCatching { lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, locListener, thread.looper) }
                .onFailure { Log.w(TAG, "GPS updates refused", it) }
        }
    }

    fun stop() {
        runCatching { lm?.removeUpdates(locListener) }
        runCatching { sm?.unregisterListener(baroListener) }
        thread.quitSafely()
    }

    /** The latest readings; a fix older than 5 s (tunnel, lost signal) counts as none. */
    fun sample(): Sample {
        val now = SystemClock.elapsedRealtime()
        val p = if (now - hpaAt <= 5000) hpa else Double.NaN
        val l = fix?.takeIf { now - fixAt <= 5000 } ?: return Sample(hpa = p)
        val alt = when {
            Build.VERSION.SDK_INT >= 34 && l.hasMslAltitude() -> l.mslAltitudeMeters
            l.hasAltitude() -> l.altitude
            else -> Double.NaN
        }
        val vAcc = when {
            Build.VERSION.SDK_INT >= 34 && l.hasMslAltitude() -> l.mslAltitudeAccuracyMeters.toDouble()
            Build.VERSION.SDK_INT >= 26 && l.hasVerticalAccuracy() -> l.verticalAccuracyMeters.toDouble()
            else -> Double.NaN
        }
        return Sample(
            lat = l.latitude,
            lon = l.longitude,
            speedKmh = if (l.hasSpeed()) l.speed * 3.6 else Double.NaN,
            altM = alt,
            vAccM = vAcc,
            hpa = p,
        )
    }

    companion object { private const val TAG = "PhoneSensors" }
}
