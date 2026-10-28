# Deploy from scratch — CYD 3.5" touchscreen board (`cyd35`)

This guide takes you from "the parcel arrived" to "the logger runs in the car, writes logs to the SD card, knows the
time, and (if you want) sends Telegram alerts". You do not need any tools installed or any electronics experience.
There is **no soldering and no wiring**: the board plugs into USB, the adapter plugs into the car.

Allow about an hour the first time. Most of it is waiting for downloads.

Each stage ends with a **You should now see…** check. Do not move on until the check passes; the
[troubleshooting table](#14-troubleshooting) at the end is ordered the same way as the stages.

> Windows users who are comfortable with a command line can also follow the shorter
> [flashing-windows.md](flashing-windows.md). This guide covers Windows, macOS and Linux.

---

## 1. What you are building

A small screen in the car that reads the diesel particulate filter (DPF) data of a 2014 Hyundai ix35 1.7 CRDi about once
a second, over a Bluetooth adapter plugged into the car's diagnostic (OBD) port. It shows the soot level in colour
(green, amber, red), warns you when the car is cleaning its DPF ("regeneration"), and saves every reading to a microSD
card. It only **reads** data; it never changes anything in the car. Background: [README](../README.md).

### Parts list

| Part | Notes |
|---|---|
| Bluetooth LE ELM327 OBD adapter | Amazon UK **B0DQV19QJF**. Must be Bluetooth **LE** (BLE); classic-Bluetooth-only adapters will not work. |
| CYD ESP32-S3 3.5" touch screen board | Amazon UK **B0HGQ5XMQ8** (ST77922, 320x480, speaker, microSD). Pins and details: [hardware.md](hardware.md). |
| USB-C **data** cable | Many cheap cables are charge-only and the computer will not see the board. If in doubt, use the cable from a phone you have synced to a computer. |
| microSD card, formatted **FAT32** | 32 GB or smaller is easiest (see below). exFAT does not work. |
| In-car USB power | A USB socket or 12 V USB adapter that is **switched by the ignition** (turns off when the car is off). An always-on socket keeps the board running and slowly drains the battery. |
| DS3231 RTC module (optional) | Keeps the time across power-off. Not needed: the clock can be set from your phone or WiFi. [wiring.md](wiring.md). |

### Formatting the microSD card as FAT32

Cards of **32 GB or smaller** usually come formatted as FAT32 already; if not, any computer can format them as FAT32.
Cards of **64 GB and larger** come as exFAT and must be reformatted. The logger never formats the card itself, and
only touches the `obd` folder on it ([log-format.md](log-format.md)). Formatting erases the card.

**Windows**
- 32 GB or smaller: File Explorer → right-click the card → **Format…** → File system **FAT32** → Start.
- Larger than 32 GB: Windows' own Format window does not offer FAT32 for big cards. The easiest fix is to use a 32 GB card.
  Otherwise format it on a Mac or Linux machine (below), or use a third-party "large FAT32" formatting tool. Such tools
  are not part of this project and have not been tested with it.

**macOS** (Terminal app). Find the card first, then format it. Check the disk number carefully: the wrong number erases
the wrong disk.
```bash
diskutil list                                   # find the card, e.g. /dev/disk4 (look at the size)
sudo diskutil eraseDisk FAT32 DPFLOG MBRFormat /dev/disk4
```

**Linux**. Replace `sdX` with your card (check the size in `lsblk`; the wrong letter erases the wrong disk).
```bash
lsblk                                           # find the card, e.g. /dev/sdb
sudo umount /dev/sdX?*                          # unmount any auto-mounted partitions
sudo parted /dev/sdX --script mklabel msdos mkpart primary fat32 1MiB 100%
sudo mkfs.vfat -F 32 -n DPFLOG /dev/sdX1
```

**You should now see:** the card shows up on your computer as an empty drive called `DPFLOG` (or similar), and its
properties/info say FAT32 (macOS calls it "MS-DOS (FAT32)").

---

## 2. Install the tools (once)

### Which way? VS Code + PlatformIO (recommended)

There are two ways to get the build tool, PlatformIO:

| | **VS Code + "PlatformIO IDE" extension (recommended)** | `pip install platformio` (alternative) |
|---|---|---|
| What it is | A free editor with Build / Upload / Monitor buttons | A command-line tool only |
| Python setup | The extension creates its own private Python environment; no PATH editing | You install Python, and on Windows must tick "Add to PATH" |
| Linux | Avoids the "externally-managed-environment" pip error on recent Ubuntu/Debian | Needs `pipx` or a virtual environment there |
| Same on all 3 systems | Yes | Mostly, but the commands differ |

This guide recommends **VS Code + PlatformIO IDE** because it works the same on Windows, macOS and Linux, needs no PATH
fixes, and gives you buttons instead of commands. It also has a terminal in which every `pio` command in this guide
works. If you already like the command line, the pip route in [flashing-windows.md](flashing-windows.md) is fine; all
`pio ...` commands below are the same.

### 2a. Windows 10/11
1. Install **Git**: https://git-scm.com/download/win (the default options are fine). Optional if you will use "Download ZIP" in step 3.
2. Install **VS Code**: https://code.visualstudio.com/
3. Open VS Code → Extensions (the four-squares icon on the left) → search **PlatformIO IDE** → **Install**.
   Wait until it says PlatformIO Core is installed, then restart VS Code when asked. The first install takes several minutes.
4. Driver: **none needed**. The board uses the ESP32-S3's built-in USB (USB-Serial/JTAG, vendor id 303A) and Windows
   10/11 installs it automatically. Its port is called `COM` plus a number, for example `COM5`.

### 2b. macOS
1. Open **Terminal** and install Apple's command line tools (this gives you Git and Python 3):
   ```bash
   xcode-select --install
   ```
   Click **Install** in the window that appears. If it says they are already installed, carry on.
2. Install **VS Code**: https://code.visualstudio.com/ (drag it into Applications).
3. VS Code → Extensions → search **PlatformIO IDE** → **Install**, then restart VS Code when asked.
4. Driver: **none needed**. The board appears as `/dev/cu.usbmodem…` (for example `/dev/cu.usbmodem1101`).
   Always use the `cu.` name, not `tty.`.

### 2c. Linux (Ubuntu/Debian; other distros use their own package manager)
1. Install Git and Python with its `venv` module (PlatformIO needs it):
   ```bash
   sudo apt update
   sudo apt install git python3 python3-venv curl
   ```
2. Install **VS Code**: https://code.visualstudio.com/ (the `.deb` package), then Extensions → **PlatformIO IDE** → Install.
3. Allow your user to open the board's serial port. Install PlatformIO's udev rules and join the serial groups:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
     | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
   sudo udevadm control --reload-rules && sudo udevadm trigger
   sudo usermod -a -G dialout,plugdev $USER       # Arch: use the uucp group instead of dialout
   ```
   **Log out and back in** (or reboot) so the group change takes effect.
4. Driver: **none needed**. The board appears as `/dev/ttyACM0` (or `ttyACM1`, …).

### Where to type `pio` commands
In VS Code, open the PlatformIO terminal: click the PlatformIO (alien head) icon on the left → **Quick Access** →
**Miscellaneous** → **New Terminal** (or use the small terminal icon in the blue status bar at the bottom).
In that terminal `pio` always works.

**You should now see:** in the PlatformIO terminal,
```bash
pio --version
```
prints `PlatformIO Core, version 6.…`.

---

## 3. Get the code

### Option A: Git (recommended, makes updating easy)
In the PlatformIO terminal, go to the folder where you keep projects and clone:
```bash
git clone https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3.git
cd DPFSentinel-ESP32-S3
```

### Option B: Download ZIP (no Git needed)
1. Open https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3 in your browser.
2. Click the green **Code** button → **Download ZIP**.
3. Unzip it somewhere easy, for example your Documents folder. The folder will be called `DPFSentinel-ESP32-S3-main`.

### Open it in VS Code
VS Code → **File → Open Folder…** → choose the project folder (the one that contains `platformio.ini`).
The first time, PlatformIO downloads the ESP32 toolchain and libraries. This is several hundred MB; wait for the
progress messages in the bottom-right to finish.

**You should now see:** the folder contains `platformio.ini`, `src`, `include` and `docs`, and the PlatformIO
sidebar shows a project task list with the environments **cyd35**, **cyd35_flipped**, **cyd35_landscape** (this board)
and **freenove** (the headless board, [deploy-freenove.md](deploy-freenove.md)).

### Which environment?
All three `cyd35…` environments build the same firmware for this board; they only differ in how the picture is turned
(`BOARD_ROTATION` in `src/board/board.h`):

| Environment | Screen |
|---|---|
| `cyd35` (the default) | Portrait, short edge at the top, USB-C socket at the bottom |
| `cyd35_flipped` | Portrait turned 180° (USB-C at the top). Use it if the picture is upside down once mounted. |
| `cyd35_landscape` | The older landscape layout |

---

## 4. Back up the factory firmware (once, before the first flash)

The board arrives with a demo program. Flashing DPF Sentinel replaces it. A backup lets you put the board back exactly
as it came, which matters if you ever want to return or resell it. It takes a few minutes and a 16 MB file.

1. Plug the board into the computer with the data cable.
2. Find its port:
   ```bash
   pio device list
   ```
   Look for the entry with `VID:PID=303A:1001` (or `303A` at least): `COM5` on Windows, `/dev/cu.usbmodem…` on macOS,
   `/dev/ttyACM0` on Linux.
3. Install `esptool` into PlatformIO's own Python and read the whole 16 MB flash (`0x1000000` bytes). Replace `PORT` with
   your port from step 2.

   **Windows (PowerShell / PlatformIO terminal)**
   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install esptool
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m esptool --chip esp32s3 --port PORT read_flash 0 0x1000000 cyd_factory_backup.bin
   ```
   **macOS / Linux**
   ```bash
   ~/.platformio/penv/bin/python -m pip install esptool
   ~/.platformio/penv/bin/python -m esptool --chip esp32s3 --port PORT read_flash 0 0x1000000 cyd_factory_backup.bin
   ```
   (If you installed PlatformIO with pip instead, plain `python -m pip install --user esptool` and `python -m esptool …`
   work, as in [flashing-windows.md](flashing-windows.md). Newer esptool versions also accept `read-flash`; the old
   spelling still works.)
4. If it says "Failed to connect", use the BOOT/RESET sequence in [step 6](#if-the-upload-says-failed-to-connect), then repeat.
5. Copy `cyd_factory_backup.bin` somewhere safe (cloud drive, USB stick). It must not be in the project folder if you
   later share the folder.

**To restore the factory firmware later** (same port rules, same python as above):
```bash
python -m esptool --chip esp32s3 --port PORT write_flash 0 cyd_factory_backup.bin
```
then press RESET.

**You should now see:** esptool finishes with a line like `Read 16777216 bytes … in … seconds`, and
`cyd_factory_backup.bin` is exactly 16,777,216 bytes (16 MB).

---

## 5. Configure

All your personal settings live in **one private file**, `include/secrets.h`. It is listed in `.gitignore`, so Git
never uploads it. You make it by copying the example:

**Windows (PowerShell)**
```powershell
copy include\secrets.example.h include\secrets.h
```
**macOS / Linux**
```bash
cp include/secrets.example.h include/secrets.h
```
(Or in VS Code: right-click `include/secrets.example.h` → Copy, then Paste, and rename the copy to `secrets.h`.)

Open `include/secrets.h` in VS Code and fill in the values. **Keep the double quotes.** Leave a value as `""` to switch
that feature off. Everything here is optional except changing the hotspot password.

| Setting | What it is | If you leave it empty |
|---|---|---|
| `WIFI_STA_SSID` / `WIFI_STA_PASS` | Your home WiFi name and password. Used for automatic time (NTP), the web page at home, and Telegram. | No home WiFi. Set the time from your phone instead (step 9). |
| `WIFI_STA_SSID2` / `WIFI_STA_PASS2` | A second network. Only the first network is tried at power-up; if it is not in range, the board tries again every 10 minutes, alternating between the two networks, and only while the engine is off. If you want alerts while driving, put your phone hotspot in the **first** slot and home WiFi here (see [step 11](#11-telegram-alerts-optional)). | Only the first network is used. |
| `WIFI_AP_PASS` | Password of the logger's own hotspot `DPF-Sentinel`. **At least 8 characters. Change it**: the default `change-me-please` is public, so anyone nearby could join and read your data. | — (always used) |
| `TG_BOT_TOKEN` / `TG_CHAT_ID` | Telegram alerts. See [step 11](#11-telegram-alerts-optional). | Telegram stays off. |

Important:
- WiFi networks must be **2.4 GHz**. The ESP32 cannot see 5 GHz networks. Most home routers broadcast both; if yours
  uses one name for both bands it usually works. On an iPhone hotspot turn on **Maximise Compatibility**.
- The hotspot password is printed on the serial monitor at boot (`Web UI: join Wi-Fi "DPF-Sentinel" (password …)`).
  That is your own computer, but do not post screenshots of it.
- Never paste `secrets.h` into chats, issues or screenshots.

Other settings (pins, timings, features) are in `src/config.h`. For a first install you do not need to change anything
there. The ones you might touch later are `ENABLE_TELEGRAM` (step 11), `BLE_SCAN_ONLY` / `BLE_TARGET_ADDRESS` (step 8)
and `TZ_RULE` (time zone, UK by default).

**You should now see:** `include/secrets.h` exists, has your values in quotes, and `WIFI_AP_PASS` is no longer
`change-me-please`.

---

## 6. Build and flash

1. Insert the FAT32 microSD card into the board (so logging can be checked straight away).
2. Plug the board into the computer.
3. Build and upload. In VS Code click the **→ (Upload)** arrow in the blue status bar (or PlatformIO sidebar →
   **cyd35** → **Upload**). From the PlatformIO terminal it is:
   ```bash
   pio run -e cyd35 -t upload          # or -e cyd35_flipped / -e cyd35_landscape (step 3)
   ```
   `cyd35` is the default environment, so plain `pio run -t upload` builds the same thing. The first build takes
   several minutes. Later builds are much faster.
4. If PlatformIO picks the wrong port, name it:
   ```bash
   pio run -e cyd35 -t upload --upload-port COM5                  # Windows
   pio run -e cyd35 -t upload --upload-port /dev/cu.usbmodem1101  # macOS
   pio run -e cyd35 -t upload --upload-port /dev/ttyACM0          # Linux
   ```
5. Close any serial monitor before uploading; only one program can use the port at a time.

### If the upload says "Failed to connect"
Put the chip into download mode by hand:
1. Hold the **BOOT** button.
2. Tap **RESET** (sometimes labelled RST), or unplug and replug the USB cable while still holding BOOT.
3. Release BOOT.
4. Run the upload again.
5. When it finishes, press **RESET** once to start the new program.

The exact position of the buttons depends on the board revision; look for the BOOT and RST/RESET labels printed next
to them.

**You should now see:** the upload ends with `Hard resetting via RTS pin...` and `[SUCCESS]`. The board restarts,
plays a short tune (the boot jingle) and the screen lights up.

---

## 7. First boot check on the bench

Open the serial monitor: the **plug** icon in the blue status bar, or
```bash
pio device monitor
```
(The speed, 115200, is already set in `platformio.ini`. Opening the monitor restarts the board and starts a new session
file, which is normal. Press `Ctrl+C` to close it.)

### Expected serial lines
In this order, with other lines in between:
```
Boot: reset reason 1 (1=power-on 3=sw 4=panic 5/6/7=watchdog 9=brownout)
DPF Sentinel | board=cyd35 | storage=SD card | wifi=none configured | telegram=off
RTC: no DS3231 found (clock will come from NTP/phone)
[boot] mounting storage (SD card)...
Logging to SD as session_1.csv (+ raw_1.log)
[boot] starting WiFi + web page...
Web UI: join Wi-Fi "DPF-Sentinel" (password ...), then open http://192.168.4.1
[boot] setup finished
Connecting to BLE ELM327 adapter...
Scanning for a BLE OBD adapter...
```
Notes:
- The `storage=` field in the banner only shows the **setting**. Whether the card actually works is the
  `Logging to …` line: `Logging to SD` is good; `Logging to flash` means the card was not found or is not FAT32, and
  only about 30 minutes fit in internal flash.
- `wifi=` shows your `WIFI_STA_SSID`, or `none configured`.
- The session number goes up by one on every boot, and keeps counting after re-flashing.
- Away from the car you will then see `No BLE adapter found …` and `Adapter link down — reconnecting...` every few
  seconds, plus `WIFI not connected`. **That is normal on the bench.**
- If you filled in home WiFi, after up to 20 seconds you should see `Wi-Fi "MyHomeWifi" joined: http://192.168.x.x
  (or http://obd-logger.local)`, then `Clock set from ntp: …`. If the network is not in range you see
  `Wi-Fi not in range — hotspot only`; the board looks again every 10 minutes while the engine is off.

### What the screen shows
The first page is the soot page. With no car connected the background is **grey** and it says **NO LOGGER DATA**.
Five small dots at the bottom show which page you are on.

### Demo mode
Type a capital **`D`** in the serial monitor (no Enter needed). The board prints `demo ON` and cycles through fake
readings every 8 seconds: no data, green, amber, red, darker red, and an active regeneration, with the matching
beeps. Type `D` again to stop (`demo OFF`). Capital `G` starts the demo on the regeneration screen. Demo values are
only shown on the screen; they are not written to the log.

### Serial command keys
Type a single character in the serial monitor. They are case-sensitive.

| Key | Does |
|---|---|
| `d` | Dump every log file to the serial monitor (see step 12) |
| `p` | Show the last 240 rows of the previous session |
| `D` | Demo mode on/off |
| `G` | Demo mode, starting on the regeneration screen |
| `a` / `r` | Play the amber / red soot alert |
| `s` / `l` | Play the regeneration start / end alert |
| `1` / `2` | Play boot tune 1 / 2 |
| `O` (capital o) | "I just changed the oil": records the current odometer and zeroes the regens-since-oil counter. Only works while the car is connected. |
| `e` | **Erase all logs** in `/obd`. Cannot be undone. Reboot afterwards. |

**You should now see:** `Logging to SD as session_N.csv`, `Connecting to BLE ELM327 adapter...`, a grey
NO LOGGER DATA screen, and the demo working when you type `D`.

---

## 8. In the car

### Fit it
1. Find the OBD port. It is a 16-pin socket, almost always under the dashboard on the driver's side, within reach of
   the steering wheel (sometimes behind a small cover). Your car's handbook shows where.
2. Plug in the BLE ELM327 adapter. It usually lights up.
3. Power the board from the **switched** USB socket. Mount it where it does not block your view, the airbags or the
   controls, and route the cable away from the pedals.
4. Turn the ignition **on** (engine running is best; some data only appears with the engine running).

### Pairing: nothing to do in your phone's Bluetooth settings
The board finds the adapter by itself. It scans for about 10 seconds and picks the strongest device whose name
contains a typical adapter name (OBD, ELM, VLINK, VEEPEAK, IOS-, VGATE, KONNWEI, OBDLINK and a few others).
After the first connection it reconnects by address.

Only **one** device can talk to the adapter at a time. Close Car Scanner or any other OBD app on your phone, or switch
the phone's Bluetooth off, while testing.

To watch it happen, take a laptop to the car with `pio device monitor` running. Expected:
```
Scanning for a BLE OBD adapter...
  candidate: aa:bb:cc:dd:ee:ff ("OBDII") rssi=-48
Using adapter aa:bb:cc:dd:ee:ff
Using service ...
ELM327 adapter ready
t=12s rpm=812 spd=0 load=21% dpfP=3.5hPa soot=9.8g ...
```
One `t=…` line appears every second while logging.

### If it does not find the adapter
1. In `src/config.h` set `#define BLE_SCAN_ONLY 1`, upload, and open the monitor near the car with the ignition on.
   In this mode the board **only scans and logs nothing**. It prints every Bluetooth LE device it hears; adapters
   are marked `<-- looks like an OBD adapter`.
2. Copy the adapter's address (like `aa:bb:cc:dd:ee:ff`) into `#define BLE_TARGET_ADDRESS "aa:bb:cc:dd:ee:ff"`.
   (`BLE_NAME_HINT` is an alternative: part of the adapter's name.)
3. Set `BLE_SCAN_ONLY` back to `0` and upload again.

Pinning the address is also a good idea if neighbours or other cars nearby have adapters.

**You should now see:** `ELM327 adapter ready` on the monitor, and on the screen the soot value with a coloured
background and live RPM, speed and temperatures. Without a laptop: the grey "NO LOGGER DATA" screen changes to a
coloured soot screen within about a minute.

---

## 9. Set the clock, and the web page

The board has no battery-backed clock unless you fit a DS3231. Logs are still written without the time; they just have
an empty `unix_time` column until the clock is known (step 12 shows how to fix that afterwards). There are three ways to
set it, tried automatically:

| Source | How | When |
|---|---|---|
| DS3231 RTC (optional) | Fitted per [wiring.md](wiring.md); read at every boot | Immediately |
| NTP (internet time) | Automatic when the board joins your home WiFi or phone hotspot from `secrets.h` | Seconds after joining |
| Your phone | Open the web page on the `DPF-Sentinel` hotspot (below) | When you open the page |

### From your phone (works anywhere, no internet needed)
1. On your phone, join the WiFi network **`DPF-Sentinel`** with the password you put in `WIFI_AP_PASS`.
   The phone may warn "no internet connection": choose to stay connected.
2. Open **http://192.168.4.1** in the browser (type `http://`, it is not https).
3. Opening the page sends the phone's time to the board. That's all.

The phone's time is accepted only if the clock is not set yet, or was set from the DS3231 (the phone then replaces it
and the DS3231 is updated). Once NTP or the phone has set the clock, later page visits leave it alone until the next
power-up. All logged times are stored in UTC; the screen shows UK local time
(`TZ_RULE` in `src/config.h`).

### Tour of the web page ("OBD Logger")
- **Top right:** the board's clock (or "clock not set") and a status pill: `adapter not connected`,
  `adapter ok · engine off`, or `live`.
- **Banner:** "DPF regeneration in progress" while the car is regenerating, or a note when the DPF pressure at idle is
  20 hPa or more.
- **Tiles:** RPM, speed, coolant, load, DPF pressure, soot, exhaust temperature before the DPF, catalyst temperature,
  miles since the last regeneration, intercooler, MAF, battery voltages, EGR. Updated every second.
- **Next regen & mileage:** the next-regen estimate and the dash mileage, plus a box to enter your dash mileage
  (see [below](#next-regen-estimate-and-dash-mileage)).
- **Last 2 minutes:** small charts of DPF pressure, exhaust temperature and RPM (only while the page is open).
- **Log files:** every `session_N.csv` and `raw_N.log`, newest first. Tap a name to download it.
- **Footer:** session number, storage (`SD` or `flash`), free space, uptime, home-network IP and clock source.

There is no delete button on the page; files can be removed with a card reader or the serial `e` key.

On your home network the same page is at the IP shown on the serial monitor and the SYSTEM page, or at
**http://obd-logger.local** (works on most computers and iPhones; some Android phones do not support `.local` names).

### Endpoints (for the curious)
| Address | Gives |
|---|---|
| `/` | The page above |
| `/api/live` | Current values, link state, session, clock, storage, next-regen estimate and `odometer_offset_mi`, as JSON |
| `/api/odometer?dash=85000` | Set the dash-mileage offset from your dash reading (engine on, adapter connected); [odometer-offset.md](odometer-offset.md) |
| `/api/files` | List of log files with sizes |
| `/dl?f=session_3.csv` | Download one file |
| `/api/time?ms=<unix ms>` | Set the clock (the page does this for you) |
| `/api/testalert` | Queue a test Telegram alert |
| `/api/send` | Send the daily Telegram report now |
| `/api/statusnow` | Send a Telegram live-status message now |
| `/api/report` | Telegram report status (`?reset` forgets what was already sent) |

The Telegram endpoints do nothing unless Telegram is enabled (step 11), and Telegram needs real internet, which the
`DPF-Sentinel` hotspot does not have.

### Next-regen estimate and dash mileage
The soot page, the HEALTH page and the web page show an estimate of the miles to the next regeneration. It is worked
out from how fast soot has been building up, counting down to the soot level at which this car's regens have been
starting. It is an estimate, not a value the ECU reports. Below **12 g** of soot it shows **"not soon"** instead of a
number, because when tested against logged drives the estimate was typically 30 or more miles out at that stage.

The dash odometer and the ECU's odometer are often two different counters and can differ by a few miles to a few
hundred. To make the screen match your dash, open the web page with the engine running and the adapter connected,
type the dash mileage into the box under **Next regen & mileage** and tap **Save**. The board stores the difference
(no reflash needed) and shows "Dash mileage (ECU + offset)" from then on. The logged CSV keeps the ECU's raw value.
Details: [odometer-offset.md](odometer-offset.md).

**You should now see:** the web page's clock shows the right time, the SYSTEM page on the board says
**Time sync: synced**, and the serial monitor printed `Clock set from phone: …` (or `ntp` / `rtc`).

---

## 10. Using the screen

### Changing pages
- Tap the **right third** of the screen for the next page, the **left third** for the previous one. The middle third
  does nothing. Pages wrap around.
- **Hold anywhere for about 0.7 s** to mute or unmute the beeps. A small "muted" appears bottom-left. Mute resets at
  the next power-up.

| Page | Shows |
|---|---|
| **Soot** (first) | Soot level in grams, large, plus RPM, speed, coolant, DPF pressure, catalyst temp, intercooler, MAF, miles since regen, regen state, odometer and IP |
| **SOOT TREND** | Soot over the last 4 hours (one point a minute), with lines at 14 g and 17 g |
| **TODAY** | Since the board powered up: miles driven, soot at start and peak, max speed, regenerations, last regen duration and peak exhaust temperature |
| **HEALTH** | Regens today / this week (needs the clock set), miles since and average between regens, regens since the oil-change reference, warm-up times, battery voltage (amber below 12.2 V) |
| **SYSTEM** | WiFi and IP, Bluetooth link, storage and session number, clock and time sync, uptime, memory |

The storage line on the SYSTEM page shows `SD 0MB` by default; free space is only measured when `SD_FREE_CHECK` is set
to 1 in `src/config.h` (it is slow on big cards). The web page footer shows free space.

### Soot colours
| Background | Meaning |
|---|---|
| Grey, "NO LOGGER DATA" | No data has arrived since power-up |
| Grey, "NO LIVE DATA" | Data stopped (adapter link lost or ECU asleep) |
| Green | Below 14 g |
| Amber (dark text) | 14 g to below 17 g |
| Red, getting darker towards 28 g | 17 g and above |

The 14 g and 17 g thresholds are not verified against Hyundai data ([pids.md](pids.md)). Treat them as a guide.

### Regeneration banner
While the car is regenerating the screen turns dark and shows **ACTIVE REGENERATION** and
**DO NOT SWITCH OFF UNTIL IT FINISHES**, with the key readings. During a regeneration the screen stays on this page and
taps do not change it. If you can, keep driving (ideally at a steady speed) until it finishes.

### Beeps
- A short tune at power-up.
- An alert when soot rises past 14 g (amber) and past 17 g (red). It does not repeat while the value hovers around the
  line (0.5 g margin).
- An alert when a regeneration starts and when it ends.

### Dimming
After 60 seconds without a touch the backlight dims. The first touch only wakes it (it does not change the page). The
screen never dims during a regeneration or while soot is 17 g or more.

**You should now see:** tapping the right edge steps through the five pages, and the dots at the bottom follow.

---

## 11. Telegram alerts (optional)

Telegram sends a message to your phone when a regeneration starts, finishes or is interrupted, plus a daily summary with
the log files attached. The full plain-English setup is in **[telegram-setup.md](telegram-setup.md)**. In short:

1. Create a bot with @BotFather and note the **token**; message your bot; find your **chat id**.
2. Put both in `include/secrets.h` (`TG_BOT_TOKEN`, `TG_CHAT_ID`), plus a 2.4 GHz WiFi with internet
   (your phone hotspot in `WIFI_STA_SSID` if you want alerts in the car, home WiFi in `WIFI_STA_SSID2`).
3. **On this board Telegram is off by default.** In `src/config.h` change `#define ENABLE_TELEGRAM 0` to
   `#define ENABLE_TELEGRAM 1`. A token without a colon, or a token without a chat id, stops the build with a message.
4. Upload again (step 6). The serial banner should now say `telegram=on`.

Things worth knowing, from the code:
- Nothing is sent until the board is on a network with internet **and** its clock is set (NTP does this right after
  joining). Alerts raised while offline are held in memory (a few at most) and sent when the board gets online; they are lost if the board loses power first.
- Networks are only looked for at power-up and then every 10 minutes **while the engine is off**. At power-up the
  board tries the first network for 20 seconds; if that fails, the next attempt (10 minutes later, engine off) tries
  the other network, and so on. If it loses a network while driving, it does not look again until the engine is off. So in the car, the phone hotspot is most
  reliably picked up when it is already on at the moment the board powers up, and is in `WIFI_STA_SSID` rather than
  `SSID2` if you mostly want alerts while driving.
- You can send `/status` to your bot to get a live status reply when the board is online.

**You should now see:** a Telegram message "Wi-Fi joined: <name> …" within about a minute of the board joining your
network.

---

## 12. Getting the logs off

Files live in the `obd` folder on the card ([log-format.md](log-format.md)):
- `session_N.csv`: one row per second, the data you want.
- `raw_N.log`: every raw adapter exchange, for debugging, plus the `TIME` marker used below.

Four ways to get them:

1. **SD card reader (simplest, fastest).** Power the board off, take the card out, put it in a computer, open the
   `obd` folder. Put the card back before the next drive.
2. **Web page.** Join `DPF-Sentinel` (or open the page on your home WiFi), scroll to **Log files**, tap a file. Big
   files take a while over WiFi.
3. **Serial dump.** With the board on USB, open the monitor with logging to a file:
   ```bash
   pio device monitor --filter log2file
   ```
   Type `d`. Every file is printed between `===== BEGIN name (N bytes) =====` and `===== END name =====`, then
   `--- dump complete ---`. The captured text is saved as a `platformio-device-monitor-….log` file in the project
   folder; cut out the part between BEGIN and END to get each file. This is the slowest way; use it when the card
   cannot be removed.
4. **Over Wi-Fi with a script.** With your computer on the same network as the board (home WiFi, or joined to
   `DPF-Sentinel`), this downloads every log file it does not already have (Python 3, no extra packages):
   ```bash
   python3 tools/fetch_board_logs.py                          # uses http://obd-logger.local
   python3 tools/fetch_board_logs.py http://192.168.4.1       # on the DPF-Sentinel hotspot
   ```
   Files go to `logs/cyd_<date>/` in the project folder (git-ignored).

### Dating rows logged before the clock was set
Rows logged before the clock was known have an empty `unix_time`. Once the clock is set, a `TIME` line is written to the
raw log, and this tool fills in the earlier rows. It needs Python 3 (`python` on Windows, `python3` on macOS/Linux;
on Windows you can use the PlatformIO Python from step 4 if `python` is not found):
```bash
python3 tools/date_session.py session_12.csv raw_12.log > session_12_dated.csv
```
It prints how many rows it dated. If the clock was never set during that session, there is no marker and the file is
copied unchanged.

**You should now see:** a `session_N.csv` you can open in Excel, Numbers or LibreOffice, with one row per second and
column names in the first row.

---

## 13. Updating the firmware later

Your `include/secrets.h` is never touched by updates (Git ignores it), so you do not need to re-enter it.

**If you used Git**
```bash
cd DPFSentinel-ESP32-S3
git pull
pio run -e cyd35 -t upload
```
If `git pull` complains that your local changes would be overwritten (usually because you edited `src/config.h`, for
example `ENABLE_TELEGRAM`), keep your changes like this:
```bash
git stash
git pull
git stash pop
```
If `git stash pop` reports a conflict, open `src/config.h`, keep the new file's lines plus your own setting, and save.

**If you used Download ZIP**
Download a new ZIP (step 3), unzip it, copy your old `include/secrets.h` into the new `include` folder, re-apply any
`src/config.h` changes you had made (for example `ENABLE_TELEGRAM 1`), then open the new folder in VS Code and upload.

Re-flashing does not erase the SD card, and session numbers carry on from where they were.

**You should now see:** the upload succeeds and the boot lines from step 7 appear again.

---

## 14. Troubleshooting

| Stage | Symptom | Fix |
|---|---|---|
| SD card | Format window has no FAT32 option (Windows) | Card is bigger than 32 GB. Use a 32 GB card, or format it on macOS/Linux (step 1). |
| Tools | `pio` not found | Use the PlatformIO terminal inside VS Code (step 2). If you used pip, try `python -m platformio`. |
| Tools | PlatformIO IDE install fails on Linux | Install `python3-venv` (step 2c), restart VS Code. |
| Tools | VS Code still "installing PlatformIO Core" | Wait; it downloads a lot the first time. Check your internet/proxy. |
| Connect | No port in `pio device list` | Use a **data** cable; try another USB port; on Linux log out and in after the dialout step. No driver is needed (native USB, VID 303A). |
| Connect | Linux: `Permission denied: '/dev/ttyACM0'` | udev rules and dialout group (step 2c), then log out and back in. |
| Connect | macOS: port not listed | Look for `/dev/cu.usbmodem…`; try another cable or a USB-C port directly on the Mac (not a hub). |
| Backup/Flash | `Failed to connect to ESP32-S3` | BOOT + RESET sequence (step 6), then retry. |
| Backup/Flash | `Could not open port … busy` / access denied | Close the serial monitor (and any other serial program), then retry. |
| Backup/Flash | Antivirus blocks esptool | Allow esptool / python in your antivirus. |
| Build | Errors about `TG_BOT_TOKEN` or `TG_CHAT_ID` | Token must be `number:letters` (with the colon); chat id must be filled when a token is. See [telegram-setup.md](telegram-setup.md). |
| Build | Downloads fail | Check internet/proxy and run again; PlatformIO resumes. |
| First boot | `Logging to flash` instead of `Logging to SD` | Card missing, not fully inserted, or not FAT32 (exFAT does not work). Reformat (step 1). |
| First boot | `Storage init failed — logging to Serial only` | Neither the card nor internal flash could be used. Re-insert the card, reboot; if it persists, report it. |
| First boot | Garbled characters in the monitor | Speed must be 115200 (already set in `platformio.ini`). |
| First boot | First lines missing | Normal with native USB: the board waits only 3 s for the monitor. Close and reopen the monitor (this restarts the board). |
| First boot | Picture upside down or sideways | Upload `cyd35_flipped` or `cyd35_landscape` instead (step 3). |
| First boot | Screen stays black | Check the upload succeeded; look for `Display init failed` on the monitor; press RESET. |
| First boot | No sound | Look for `Audio init failed`; check the small "muted" marker (hold 0.7 s to unmute). |
| Car | `No BLE adapter found` repeating | Ignition on; adapter fully in the port; close Car Scanner/other apps; use `BLE_SCAN_ONLY` and pin `BLE_TARGET_ADDRESS` (step 8). |
| Car | `No adapter-like names seen` | Your adapter has an unusual name. Use the list printed under it, or `BLE_SCAN_ONLY`, and pin the address. |
| Car | `BLE connect() failed` | Another device is connected to the adapter, or it is out of range. Unplug and replug the adapter. |
| Car | `Could not find a notify + write characteristic pair` | Unusual adapter. Set `BLE_DUMP_SERVICES 1` in `src/config.h` to list its services ([development.md](development.md)). |
| Car | `ELM327 adapter ready` but values stay empty | Start the engine; the ECU may be asleep with only the ignition on. After 10 empty seconds it re-initialises the adapter, after 30 it reconnects. |
| Car | Only standard values (RPM etc.), no soot | Car is not a 2014 ix35 1.7 CRDi. Other cars are not supported yet ([pids.md](pids.md)). |
| Car | Board keeps running after the car is off | The USB socket is always-on. Use a switched socket. |
| Clock | Web page says "clock not set" | Reload the page with the phone on `DPF-Sentinel`; the phone's own clock must be right. |
| Clock | Phone time not taken | The clock was already set this session by NTP or an earlier page visit. That is fine. |
| Clock | Wrong hour on screen | Logs are UTC; the screen uses `TZ_RULE` (UK by default). |
| Web | `192.168.4.1` does not open | Make sure the phone is still on `DPF-Sentinel` (phones sometimes drop "no internet" networks); type `http://`, not `https://`; turn mobile data off briefly. |
| Web | Can't find `DPF-Sentinel` | Check `WIFI_AP_PASS` is at least 8 characters, re-upload, and look for the `Web UI: join Wi-Fi` line on the monitor. |
| WiFi | Never joins home WiFi | 2.4 GHz only; name and password are case-sensitive; the board only looks at power-up and every 10 minutes while the engine is off. |
| Web | Dash-mileage Save says "no live odometer reading yet" | The engine must be running and the adapter connected. |
| Telegram | Nothing arrives | See the table in [telegram-setup.md](telegram-setup.md). Also check `ENABLE_TELEGRAM 1` and that the banner says `telegram=on`. |
| Logs | `unix_time` column empty | Clock was not set yet. Use `tools/date_session.py` (step 12), and set the clock earlier next time. |
| Logs | Many session files | A new session starts at every power-up and every time the serial monitor is opened. That is expected. |

---

## 15. Safety and legal

- **Read-only.** The logger only asks the car for data. It does not clear fault codes, force regenerations, or change
  any setting. It is the opposite of DPF/EGR "delete" tuning, which is illegal for road use and harmful.
- **Not a diagnostic tool.** Values can be wrong or missing. Do not base safety or repair decisions on it; ask a
  qualified mechanic. The soot colour thresholds and the idle-pressure warning are unverified estimates.
- **Only the 2014 Hyundai ix35 1.7 CRDi is verified** ([pids.md](pids.md)). On other cars most readings will be empty.
- **Do not use the screen while driving.** Mount it so it does not block your view, the airbags or the controls, and
  keep cables away from the pedals. Follow your local laws about screens in vehicles.
- An OBD adapter left plugged in draws a little power even with the car off. If the car will stand for weeks, unplug it.
- Keep `include/secrets.h` and your Telegram token private. If the token leaks, revoke it with @BotFather.
- Full disclaimer: [disclaimer.md](disclaimer.md).

Licence: GPL-3.0-or-later. If this saves you a DPF job, you can support the project on [Ko-fi](https://ko-fi.com/vitoi).
