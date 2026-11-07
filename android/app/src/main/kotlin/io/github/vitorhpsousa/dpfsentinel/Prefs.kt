package io.github.vitorhpsousa.dpfsentinel

import android.content.Context
import android.content.SharedPreferences
import io.github.vitorhpsousa.dpfsentinel.core.Units

// Settings and small persistent counters (the phone's equivalent of the firmware's NVS keys).
class Prefs(context: Context) {
    private val p: SharedPreferences = context.applicationContext.getSharedPreferences("dpf", Context.MODE_PRIVATE)

    var adapterAddress: String
        get() = p.getString("adapter", "") ?: ""
        set(v) = p.edit().putString("adapter", v).apply()

    var adapterName: String
        get() = p.getString("adapterName", "") ?: ""
        set(v) = p.edit().putString("adapterName", v).apply()

    var tgToken: String
        get() = p.getString("tgToken", "") ?: ""
        set(v) = p.edit().putString("tgToken", v.trim()).apply()

    var tgChatId: String
        get() = p.getString("tgChat", "") ?: ""
        set(v) = p.edit().putString("tgChat", v.trim()).apply()

    val telegramEnabled get() = tgToken.contains(':') && tgChatId.isNotEmpty()

    /** Daily report time (local), like TG_SEND_HOUR/TG_SEND_MIN. */
    var reportHour: Int
        get() = p.getInt("tgHour", 17)
        set(v) = p.edit().putInt("tgHour", v).apply()

    var reportMinute: Int
        get() = p.getInt("tgMin", 30)
        set(v) = p.edit().putInt("tgMin", v).apply()

    var tgLastReportDay: Long
        get() = p.getLong("tgDay", 0)
        set(v) = p.edit().putLong("tgDay", v).apply()

    var tgLastSentSession: Int
        get() = p.getInt("tgBoot", 0)
        set(v) = p.edit().putInt("tgBoot", v).apply()

    var tgUpdateOffset: Long
        get() = p.getLong("tgOffset", 0)
        set(v) = p.edit().putLong("tgOffset", v).apply()

    /** Dash minus ECU mileage, applied only to what is shown (docs/odometer-offset.md). */
    var odometerOffsetMi: Float
        get() = p.getFloat("odoOff", 0f)
        set(v) = p.edit().putFloat("odoOff", v).apply()

    /** How speed, distance and temperature are shown (never changes the logs). */
    var units: Units
        get() = Units.fromName(p.getString("units", null))
        set(v) = p.edit().putString("units", v.name).apply()

    /** Start logging when Android Auto opens the app. */
    var autoStartWithCar: Boolean
        get() = p.getBoolean("autoCar", true)
        set(v) = p.edit().putBoolean("autoCar", v).apply()

    /** Write latitude/longitude to the logs (speed, altitude and grade are logged regardless). */
    var logPosition: Boolean
        get() = p.getBoolean("logPos", true)
        set(v) = p.edit().putBoolean("logPos", v).apply()

    /**
     * Never reuses a number: also above any log file already on the phone, and saved with commit()
     * so a kill right after starting can't lose the increment (apply() writes later).
     */
    fun nextSessionNumber(highestOnDisk: Int = 0): Int {
        val n = maxOf(p.getInt("session", 0), highestOnDisk) + 1
        p.edit().putInt("session", n).commit()
        return n
    }

    val lastSessionNumber get() = p.getInt("session", 0)

    var health: String
        get() = p.getString("health", "") ?: ""
        set(v) = p.edit().putString("health", v).apply()
}
