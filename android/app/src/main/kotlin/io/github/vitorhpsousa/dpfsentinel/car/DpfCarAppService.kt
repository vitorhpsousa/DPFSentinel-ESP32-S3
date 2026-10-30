package io.github.vitorhpsousa.dpfsentinel.car

import android.content.Intent
import android.content.pm.ApplicationInfo
import androidx.car.app.CarAppService
import androidx.car.app.Screen
import androidx.car.app.Session
import androidx.car.app.validation.HostValidator
import io.github.vitorhpsousa.dpfsentinel.Prefs

// Entry point Android Auto binds to. Android Auto only lists it when Google Play is the recorded
// installer (KingInstaller) and Unknown sources is on: see android/README.md.
class DpfCarAppService : CarAppService() {
    override fun createHostValidator(): HostValidator =
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) {
            HostValidator.ALLOW_ALL_HOSTS_VALIDATOR
        } else {
            HostValidator.Builder(applicationContext)
                .addAllowedHosts(androidx.car.app.R.array.hosts_allowlist_sample)
                .build()
        }

    override fun onCreateSession(): Session = object : Session() {
        override fun onCreateScreen(intent: Intent): Screen {
            val screen = LiveScreen(carContext)
            if (Prefs(carContext).autoStartWithCar) screen.startLogging(quiet = true)
            return screen
        }
    }
}
