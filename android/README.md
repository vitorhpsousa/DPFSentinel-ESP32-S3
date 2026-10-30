# DPF Sentinel for Android (and Android Auto)

The phone does the board's job: it connects straight to the BLE ELM327 adapter, reads the same PIDs,
writes the same `session_N.csv` / `raw_N.log` files, and shows soot, the next-regen estimate and regen
state on the phone and on the Android Auto screen. Regen and health alerts go to notifications and,
optionally, Telegram. No ESP32 needed.

Use the phone **or** the board, not both: a BLE ELM327 accepts one connection at a time.

Only the 2014 Hyundai ix35 1.7 CRDi is verified (same PID table as the firmware, [docs/pids.md](../docs/pids.md)).
Read-only, not a diagnostic tool: see [docs/disclaimer.md](../docs/disclaimer.md).

## Get the APK

Every push that touches `android/` builds it on GitHub Actions:

1. Open the repo's **Actions** tab → **Android app** → the latest green run.
2. Download the **dpf-sentinel-apk** artifact (a zip) and unzip it.
3. Copy `app-debug.apk` to the phone and open it. Android asks to allow installs from that app (Files/Chrome) once.

Or build it yourself with Android Studio (open the `android/` folder), or from a terminal with the Android SDK:

```bash
cd android
./gradlew :app:assembleDebug      # -> app/build/outputs/apk/debug/app-debug.apk
./gradlew -p core test            # decoding / regen / health tests, no Android SDK needed
```

## First run

1. Open DPF Sentinel and allow **Nearby devices** (Bluetooth) and **notifications**.
2. Plug the adapter in, ignition on, and close Car Scanner or any other OBD app.
3. **Start logging**. The first adapter found with an OBD-like name (OBD, ELM, Vlink, Konnwei, OBDLink…)
   is used and remembered; *Settings → Forget adapter* makes it scan again.

Logging keeps going with the screen off (a foreground service with a notification). **Stop** ends the session.

## Android Auto

A DPF monitor doesn't fit any of the app categories Google allows on the Play Store for cars, so this
works as a **sideloaded** app:

1. Phone: Settings → Apps → Android Auto → *Additional settings in the app* (or open Android Auto).
2. Scroll to **Version** and tap it ~10 times until developer mode is enabled.
3. ⋮ menu → **Developer settings** → tick **Unknown sources**.
4. Connect to the car. DPF Sentinel appears in the app launcher on the car screen (customise the
   launcher in Android Auto's settings if it's hidden).

The car screen shows soot (with a green / amber / red dot and the word OK / rising / HIGH), the
next-regen estimate or **REGENERATING · N min**, EGT and DPF pressure, miles since the last regen,
and the adapter status. It refreshes every 2 seconds (Android Auto limits how often an app may redraw).
Regen start/finish and high-soot alerts pop up as notifications.

Opening the app on the car starts logging when *Settings → Start logging when Android Auto opens DPF
Sentinel* is on. Some Android versions refuse to start a background service from the car screen alone;
if the car shows "Start logging from the phone app", tap Start on the phone once.

## Telegram

*Settings → Telegram*: paste the bot token and your chat id ([docs/telegram-setup.md](../docs/telegram-setup.md)
explains how to get both), **Save**, then **Send test**. You then get:

- regen started / finished / interrupted, and the health warnings (3 fast regens, oil reminder, slow warm-up),
  queued while the phone is offline;
- a daily report after the set time (17:30 by default) with the newest session logs not sent yet
  (sessions under 3 KB are skipped; **Report now** sends everything new);
- an answer to `/status` sent from your chat (polled every minute while logging).

Still-running pings every 30 s during a regen go to the screens only, not to Telegram.

## Logs

*Logs* lists the files; **Share** sends one to Drive, email, Telegram, etc. They also sit in
`Android/data/io.github.vitorhpsousa.dpfsentinel/files/obd` (visible over USB). The format is the
firmware's ([docs/log-format.md](../docs/log-format.md)); `unix_time` is always filled because the phone
knows the time, and `millis` counts from the start of the session.

## Code layout

```
core/   pure Kotlin, a port of the firmware logic: ISO-TP reassembly, PID table + decoders,
        request scheduler, regen watch, health + next-regen estimate, CSV format, message texts.
        Tested against the firmware's golden vectors (test/test_decode).
app/    Android: BLE link (obd/BleElm.kt), LoggerService (the main loop), Telegram, notifications,
        the phone UI (ui/, Compose) and the Android Auto screen (car/, Car App Library).
```

Changes to PIDs or thresholds should be made in both `src/` (firmware) and `android/core/` so the two stay in step.
