# DPF Sentinel for Android: privacy policy

DPF Sentinel is a read-only OBD-II logger for a diesel particulate filter. This policy covers the
Android app (`io.github.vitorhpsousa.dpfsentinel`).

**What the app reads.** Engine and exhaust values (soot level, pressures, temperatures, speed, mileage
and similar) from a Bluetooth ELM327 adapter plugged into your own car.

**Where it goes.**
- Log files are saved on your phone, in the app's own folder. They leave the phone only when you share
  one yourself.
- If you enter your own Telegram bot token and chat id, alerts, status replies and log files are sent
  to that chat through Telegram's servers. Nothing is sent to Telegram until you set this up.
- Nothing is sent to the developer or to anyone else. The app has no accounts, analytics, ads or
  tracking.

**Permissions.**
- Nearby devices / Bluetooth: to find and talk to the OBD adapter. Not used to work out where you are.
- Location (Android 11 and older only): Android requires it there for any Bluetooth LE scan. The app
  never reads your location.
- Notifications: regeneration and high-soot alerts.
- Internet: only for Telegram, if you set it up.

**Deleting data.** Delete log files from the app's Logs tab, or uninstall the app to remove everything,
including saved settings.

Questions: open an issue at https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3/issues
