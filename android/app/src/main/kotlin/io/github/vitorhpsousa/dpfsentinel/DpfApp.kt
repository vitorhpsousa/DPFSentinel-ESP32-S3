package io.github.vitorhpsousa.dpfsentinel

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager

class DpfApp : Application() {
    /** One sender for the logger and the settings screen's test/report buttons. */
    val telegram by lazy { Telegram(this) }

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CH_LIVE, "Logging", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Shown while DPF Sentinel is reading the car"
            },
        )
        nm.createNotificationChannel(
            NotificationChannel(CH_ALERTS, "DPF alerts", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Regeneration started/finished, high soot, health warnings"
            },
        )
    }

    companion object {
        const val CH_LIVE = "live"
        const val CH_ALERTS = "alerts"
    }
}
