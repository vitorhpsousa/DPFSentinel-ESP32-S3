# Deploy from scratch — Freenove headless board (`freenove`)

This guide takes you from "the parcel arrived" to "the logger runs in the car, writes logs to the SD card, knows the
time, and sends Telegram alerts". It works on **Windows, macOS and Linux**. You do not need any electronics
experience and you do not need to solder anything (unless you add the optional extras).

Budget about **1 to 1.5 hours** the first time. Most of it is downloads.

The board has no screen, so **Telegram is how it talks to you.** Set up Telegram *before* the first flash (Stage 5) and
you will only need to flash once.

Every stage ends with a **"You should now see"** check. Do not move on until the check passes. If it fails, look at
the [troubleshooting table](#14-troubleshooting) first.

| Stage | What | Time |
|---|---|---|
| [1](#1-what-youre-building-and-the-parts-list) | Parts list, prepare the microSD card | 10 min |
| [2](#2-install-the-tools) | Install the tools (Windows / macOS / Linux) | 20 min |
| [3](#3-get-the-code) | Get the code | 2 min |
| [4](#4-plug-the-board-in-the-right-socket) | Plug the board into the right USB socket | 5 min |
| [5](#5-set-up-telegram-and-secretsh) | Telegram bot and `secrets.h` | 10 min |
| [6](#6-build-and-flash) | Build and flash | 10 min |
| [7](#7-bench-test-at-home) | Bench test at home | 5 min |
| [8](#8-in-the-car) | First drive | 15 min |
| [9](#9-the-clock) - [13](#13-updating-the-firmware-later) | Clock, alerts, mounting, logs, updates | reference |
| [14](#14-troubleshooting) | Troubleshooting | reference |
| [15](#15-safety-and-legal) | Safety and legal | read once |

---

## 1. What you're building and the parts list

This guide is for the headless (no-screen) build. For the CYD 3.5" touchscreen board see
[deploy-cyd35.md](deploy-cyd35.md). Both builds come from the same code.

A small board sits in the car. It talks over Bluetooth Low Energy (BLE) to an OBD-II adapter plugged into the car's
diagnostic port. About once a second it reads the diesel particulate filter (DPF) values: soot level, filter
pressure, exhaust temperature, and whether a regeneration (the filter's self-clean) is running. It writes every
reading to a microSD card. When it can reach the internet (your phone's hotspot or home WiFi), it sends you
Telegram messages.

It is **read-only**: it asks the car questions and never changes anything. Only the **2014 Hyundai ix35 1.7 CRDi** is
verified ([pids.md](pids.md)). On other cars it will connect, but most columns will be empty.

```
 car OBD port ── BLE ELM327 adapter )))  Bluetooth  (((  ESP32-S3 board ── microSD card
                                                          │        )))  WiFi  (((  phone hotspot ── internet ── Telegram
                                                          └─ switched USB power in the car
```

### Parts

| Part | Notes |
|---|---|
| **BLE ELM327 OBD adapter** (Amazon UK B0DQV19QJF) | Must be Bluetooth **LE**. Classic-Bluetooth-only adapters will not work. |
| **Freenove ESP32-S3 WROOM N16R8 kit** (Amazon B0F48DV38M) | The logger. Two USB-C sockets and a microSD slot. The camera in the kit is not used. |
| **USB-C *data* cable** | Many cheap cables only charge. If your computer never sees the board, the cable is the first suspect. |
| **microSD card, 8 to 32 GB** | Formatted **FAT32** (not exFAT). See below. A 32 GB card holds years of logs: one hour of driving is roughly 0.6 MB of CSV plus the raw log. |
| **Switched USB power in the car** | A 12 V socket USB adapter (1 A or more is plenty) that turns **off** with the ignition, plus a USB-C cable long enough to reach where you mount the board. |
| A computer | Windows 10/11, macOS or Linux, with internet for the first build. |
| A phone with Telegram | For the alerts. Its hotspot gives the logger internet in the car. |

Optional extras (none are needed to get going):

| Extra | What for | Guide |
|---|---|---|
| DS3231 real-time clock module, plus 4 female-to-female jumper wires | Correct time from the first second, even with no WiFi. Wired to 3V3, GND, GPIO 47 (SDA), GPIO 48 (SCL). | [wiring.md](wiring.md) |
| 2 LEDs, 2 resistors (220 to 330 ohm), jumper wires | A "regen running" light and a "hot exhaust" light on GPIO 1 and GPIO 2. | [Stage 11](#11-mounting-and-power-in-the-car) |
| A small plastic box | The board comes bare. See [Stage 11](#11-mounting-and-power-in-the-car). |

### Prepare the microSD card (FAT32)

The logger only reads **FAT32**. Cards of 32 GB and smaller usually come FAT32 already. Cards of 64 GB and above come
as exFAT and must be re-formatted. **Formatting erases everything on the card.** Copy anything you need off it first.

The logger only ever writes inside a folder called `obd`, so you can share a card with other things if you want.

**Windows**
- 32 GB or smaller: File Explorer, right-click the card, **Format…**, File system **FAT32**, tick Quick Format, Start.
- Larger than 32 GB: Windows' own Format window does not offer FAT32 for big cards. Either use a 32 GB card (easiest), or
  use a free third-party FAT32 formatter such as "FAT32 Format" (guiformat). Download it only from its author's site.

**macOS** (works for any size)
1. Open **Disk Utility**. Menu **View > Show All Devices**.
2. Select the card's **device** (the top line for the card, not the indented volume under it).
3. **Erase**. Name: `DPFLOG`. Format: **MS-DOS (FAT)**. Scheme: **Master Boot Record**. Erase.

Or in Terminal (check the disk number carefully, the wrong one erases another disk):
```bash
diskutil list                                   # find the card, e.g. /dev/disk4
sudo diskutil eraseDisk FAT32 DPFLOG MBRFormat /dev/disk4
```

**Linux** (works for any size; the GNOME "Disks" app can do the same with clicks)
```bash
lsblk                                           # find the card, e.g. /dev/sdb (check the size!)
sudo umount /dev/sdb1                           # ignore "not mounted"
sudo parted /dev/sdb --script mklabel msdos mkpart primary fat32 1MiB 100%
sudo mkfs.vfat -F 32 -n DPFLOG /dev/sdb1
```

**You should now see:** the card shows as **FAT32** / **MS-DOS (FAT)** in your file manager or disk tool. Put it in the
board's microSD slot (contacts facing the board, push until it sits flush).

---

## 2. Install the tools

### The recommended path: VS Code + PlatformIO IDE

The firmware is built with **PlatformIO**. We recommend getting it as the **PlatformIO IDE** extension for
**Visual Studio Code** (both free), because:
- it is the **same on Windows, macOS and Linux**, so this guide has one set of steps;
- it installs and manages PlatformIO itself, so there are no `PATH` problems;
- you get buttons for Build / Upload / Serial Monitor, and a good editor for `secrets.h`.

The alternative is the command-line PlatformIO (installed with `pip`). It is lighter but needs Python on your `PATH`.
Both build the same firmware. Every step below shows the button **and** the `pio` command, so you can use either.

#### Windows 10/11
1. Install **VS Code**: https://code.visualstudio.com/ (defaults are fine).
2. Install **Git** (optional, see [Stage 3](#3-get-the-code)): https://git-scm.com/download/win (defaults are fine).
3. Open VS Code. Click the **Extensions** icon on the left (four squares), search **PlatformIO IDE**, click **Install**.
   Wait until it says PlatformIO is ready (a few minutes the first time), then restart VS Code when asked.

#### macOS
1. Install **VS Code**: https://code.visualstudio.com/ . Open the download and drag **Visual Studio Code** into **Applications**.
2. Git: open **Terminal** and type `git --version`. If Git is missing, macOS offers to install the Command Line
   Developer Tools. Accept.
3. Open VS Code, **Extensions**, search **PlatformIO IDE**, **Install**, wait, restart VS Code when asked.

#### Linux (Debian/Ubuntu shown; use your distro's package manager otherwise)
1. Install **VS Code** from https://code.visualstudio.com/ (`.deb` or `.rpm`), then the helpers PlatformIO needs:
   ```bash
   sudo apt update
   sudo apt install git python3 python3-venv curl
   ```
2. Open VS Code, **Extensions**, search **PlatformIO IDE**, **Install**, wait, restart VS Code when asked.

#### Alternative: command-line PlatformIO only

| OS | Install |
|---|---|
| Windows | Follow step 1 of [flashing-windows.md](flashing-windows.md) (Python with "Add to PATH", then `python -m pip install --user platformio`). |
| macOS | `brew install platformio` (if you use Homebrew), or `python3 -m pip install --user platformio` |
| Linux | `pipx install platformio` (or `python3 -m pip install --user platformio`) |

Check it with `pio --version`. If `pio` is not found, use `python -m platformio` (Windows) or `python3 -m platformio`
wherever this guide says `pio`.

> **Using VS Code but want to type commands?** Click the PlatformIO icon (the ant head) on the left, then
> **Quick Access > Miscellaneous > New Terminal**. In that terminal `pio` always works.

### USB driver for the board's COM socket

The socket you flash through (Stage 4) goes through a small USB-to-serial chip on the board. Freenove boards use a
WCH chip (CH340/CH343 family); some boards use a Silicon Labs CP210x instead. We have not confirmed the exact chip on
every batch: look at the small chip next to the **COM**/**UART** socket, the name is printed on it.

| OS | What to do |
|---|---|
| Windows | Usually installs itself through Windows Update when you plug in. If no **COM** port appears (Stage 4), install the driver for your chip: WCH (https://www.wch-ic.com, search "CH343SER" or "CH341SER") or Silicon Labs (https://www.silabs.com, "CP210x VCP drivers"). |
| macOS | Usually nothing to install. The port appears as `/dev/cu.usbserial-…`, `/dev/cu.usbmodem…` or, with WCH's own driver installed, `/dev/cu.wchusbserial…`. CP210x shows as `/dev/cu.SLAB_USBtoUART` or `/dev/cu.usbserial-…`. If none appears, install the WCH or Silicon Labs macOS driver from the links above and allow it in **System Settings > Privacy & Security**. |
| Linux | The drivers are built in. The port is `/dev/ttyUSB0` or `/dev/ttyACM0`. You need permission to use it (below). |

**Linux only: serial port permission (once)**
```bash
sudo usermod -a -G dialout $USER        # Arch/Manjaro: use the group "uucp" instead of "dialout"
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```
Then **log out and back in** (or reboot) so the group change takes effect.
On Ubuntu, if the port appears for a second and then vanishes, the `brltty` braille service grabbed it:
`sudo apt remove brltty`, then unplug and replug the board.

**You should now see:** VS Code with a PlatformIO (ant head) icon on the left. Or, for the command-line path,
`pio --version` prints a version number.

---

## 3. Get the code

**With Git** (Windows: PowerShell or the VS Code terminal; macOS/Linux: Terminal):
```bash
git clone https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3.git
cd DPFSentinel-ESP32-S3
```

**Without Git:** open https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3 , click the green **Code** button,
**Download ZIP**, and unzip it somewhere simple (for example `Documents`). The folder may be called
`DPFSentinel-ESP32-S3-main`; that is fine.

Now open the folder in VS Code: **File > Open Folder…** and pick the `DPFSentinel-ESP32-S3` folder (the one that
contains `platformio.ini`). If VS Code asks whether you trust the authors, choose **Yes**. The first time, PlatformIO
downloads the ESP32 toolchain in the background (a few hundred MB). Let it finish.

**You should now see:** in VS Code's file list, `platformio.ini`, `src`, `include`, `docs`. The blue status bar at the
bottom shows PlatformIO buttons (a tick, a right-arrow, a plug).

**Important: pick the `freenove` environment.** The project builds two boards, and the default environment is
`cyd35` (the touchscreen board). In VS Code, click the environment name in the blue status bar (it shows
`env:cyd35` or `Default`) and choose **env:freenove** before using the Build/Upload buttons. On the command line,
always add `-e freenove`, as every command in this guide does.

---

## 4. Plug the board in (the right socket)

The Freenove board has **two USB-C sockets**. Use the one marked **COM** (or **UART**) for everything: flashing *and*
reading messages. The firmware is built with `ARDUINO_USB_CDC_ON_BOOT=0` (see `platformio.ini`), which sends all its
messages to the COM socket. The other socket (marked **USB**, the chip's native USB) can power the board, but you will see
**nothing** on it.

1. Insert the FAT32 microSD card.
2. Connect the **COM** socket to your computer with the data cable.
3. Find the port:

| OS | How |
|---|---|
| Any (command line) | `pio device list` |
| Windows | Device Manager > **Ports (COM & LPT)**: a new `COMx` entry |
| macOS | `ls /dev/cu.*` before and after plugging in; the new entry is the board |
| Linux | `ls /dev/ttyUSB* /dev/ttyACM*` before and after plugging in |

Usually PlatformIO finds the port by itself, so you only need the name if it picks the wrong one.

**Should you back up the factory firmware first?** Not needed. The board arrives with Freenove's demo program, which
holds nothing of yours, and Freenove publishes their examples. If you want a copy anyway, see the optional step in
[Stage 6](#6-build-and-flash).

**You should now see:** a new port appears when you plug in, and disappears when you unplug.
No new port? Try another cable (most likely), another USB port, then the driver (Stage 2).

---

## 5. Set up Telegram and secrets.h

Do this **before** the first flash. Your passwords and Telegram details are compiled into the firmware, so if you
add them later you have to flash again. There is also a practical reason: once the logger is running with your bot
token, it reads the bot's messages itself, so the browser trick for finding your chat id (step 5.3) stops showing
anything.

### 5.1 Make a Telegram bot (summary)

Full plain-English version with pictures-in-words: [telegram-setup.md](telegram-setup.md). Short version:

1. In Telegram, open **@BotFather** (blue tick), send `/newbot`, give it a name, then a username ending in `bot`.
2. BotFather replies with a **token** like `12345678:AAbbCC…` (a number, a colon, letters). Copy the whole thing.
   **Treat it like a password.**
3. Open your new bot, press **Start**, send "hi". (The bot cannot find you until you write to it.)
4. In a browser, open `https://api.telegram.org/botYOUR-TOKEN/getUpdates` (your token after the word `bot`, no space).
   Find `"chat":{"id":` and copy the number after it. That is your **chat id**. Do not share or screenshot this page.

### 5.2 Decide which WiFi networks the logger joins

The logger needs internet to reach Telegram. It can remember **two** networks: slot 1 (`WIFI_STA_SSID`) and slot 2
(`WIFI_STA_SSID2`). Both must be **2.4 GHz** (the ESP32 cannot use 5 GHz).

How the code uses them (`src/web/web_ui.cpp`) matters for where you put your phone hotspot:
- At power-up it tries **slot 1 only**, for 20 seconds.
- If that fails, it tries the **other slot about 10 minutes later**, and only while the **engine is not running**
  (so it never disturbs the Bluetooth link while you drive).
- If it loses a network while driving, it does not look again until the engine is off (and at least 10 minutes have
  passed since it lost it).

In short: networks are looked for at power-up and then every 10 minutes, only while the engine is off.

The board is powered from a switched USB socket, so it starts when you turn the ignition on and the engine is
running a minute later. In practice, **only slot 1 gets a chance in the car**.

| Your situation | Slot 1 (`WIFI_STA_SSID`) | Slot 2 (`WIFI_STA_SSID2`) |
|---|---|---|
| You want alerts while driving (recommended) | **phone hotspot** | home WiFi |
| You only care about the daily report when parked at home with the ignition on | home WiFi | phone hotspot |

This guide uses the recommended layout: **phone hotspot in slot 1, home WiFi in slot 2.** For the bench test you then
simply switch your phone hotspot on, which also proves the in-car path works.

> [telegram-setup.md](telegram-setup.md) puts the hotspot in `WIFI_STA_SSID2`. That works only if the home
> network in slot 1 is not configured or the board stays powered with the engine off for 10 minutes. The table
> above is what the code actually does.

**Phone hotspot settings**
- iPhone: **Settings > Personal Hotspot**, turn on **Allow Others to Join** and **Maximise Compatibility** (this
  makes the hotspot 2.4 GHz, which the ESP32 needs). The hotspot name is your phone's name
  (**Settings > General > About > Name**). Avoid curly apostrophes in it: "Sam's iPhone" typed on an iPhone uses
  a curly `’`, which is hard to type correctly in `secrets.h`. Renaming the phone to something plain (e.g. `MyPhone`) avoids that.
- Android: **Settings > Network > Hotspot & tethering > WiFi hotspot**. Set the band to **2.4 GHz** if there is a
  choice. Note the name and password.
- Some phones switch the hotspot off by themselves when nothing is connected for a while. Turn it on just before you
  start the car.

### 5.3 Create and fill in secrets.h

Copy `include/secrets.example.h` to `include/secrets.h` (same folder). Edit only the copy: `secrets.h` is ignored by
Git, so your passwords never get uploaded.

Windows (PowerShell, in the project folder):
```powershell
copy include\secrets.example.h include\secrets.h
```
macOS / Linux:
```bash
cp include/secrets.example.h include/secrets.h
```
Or in VS Code: right-click `secrets.example.h` > **Copy**, right-click the `include` folder > **Paste**, then rename the
copy to exactly `secrets.h`.

Open `include/secrets.h` and fill in the quoted values. Keep the double quotes. Example (fake values):
```c
#define WIFI_STA_SSID  "MyPhone"                  // slot 1: phone hotspot (see 5.2)
#define WIFI_STA_PASS  "hotspot password"
#define WIFI_STA_SSID2 "MyHomeWifi"               // slot 2: home WiFi, 2.4 GHz
#define WIFI_STA_PASS2 "home wifi password"

#define WIFI_AP_PASS   "pick-a-new-one-8+"        // the logger's own "DPF-Sentinel" hotspot, 8+ characters

#define TG_BOT_TOKEN   "12345678:AAbbCCddEEffGGhhIIjjKKllMMnnOOppQQr"
#define TG_CHAT_ID     "123456789"
```
Rules:
- Names and passwords are **case-sensitive** and must match exactly.
- If a password contains `"` or `\`, write it as `\"` or `\\`.
- `WIFI_AP_PASS` must be **at least 8 characters**. Change it from the default `change-me-please`: that default is
  public, so anyone nearby who knows it could join the hotspot and read your data and logs.
- The token goes in as one piece, colon included. If you mistype it (no colon), or set a token without a chat id,
  the build stops with a message saying what to fix (checks at the bottom of `src/config.h`).
- Never put any of this in `src/config.h` or paste `secrets.h` into a chat, issue or screenshot.

You do **not** need to change `ENABLE_TELEGRAM`: on this board it is already `1` in `src/config.h`. With no token it
simply stays quiet.

**You should now see:** `include/secrets.h` exists and contains your values. `include/secrets.example.h` is unchanged.

---

## 6. Build and flash

1. Board plugged into the **COM** socket, microSD card inserted.
2. Close any serial monitor that is open (it holds the port).
3. Build and upload:
   - VS Code: click the **right-arrow** (PlatformIO: Upload) in the bottom status bar.
   - Command line, in the project folder:
     ```bash
     pio run -e freenove -t upload
     ```
     (Plain `pio run -t upload` would build the default `cyd35` screen firmware, which is wrong for this board.)
   If it picks the wrong port, add it explicitly:
   ```bash
   pio run -e freenove -t upload --upload-port COM5                   # Windows
   pio run -e freenove -t upload --upload-port /dev/cu.usbserial-110  # macOS (use your name)
   pio run -e freenove -t upload --upload-port /dev/ttyUSB0           # Linux
   ```

The first build downloads the ESP32 platform and libraries and takes several minutes. Later builds take under a minute.

**If the upload says "Failed to connect" or waits at "Connecting….":** put the board into download mode by hand.
Hold **BOOT**, tap **RESET** (may be labelled **RST** or **EN**), release **BOOT**, then upload again. After the upload,
tap **RESET** once to start the new program. You can also unplug the board, hold **BOOT** while plugging it back in,
then release.

**Optional (advanced): keep a copy of the factory firmware.** Not needed (see Stage 4). If you want it, run this
*after* `pio run -e freenove` (build only, which installs the tool) and *before* your first upload. It reads the whole 16 MB flash
and takes a few minutes:
```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 read_flash 0 0x1000000 freenove-factory.bin
```
To put it back later: the same command with `write_flash 0 freenove-factory.bin`.

**You should now see:** the upload ends with `SUCCESS` (VS Code also shows it in green).

---

## 7. Bench test at home

Switch your **phone hotspot on** (slot 1, Stage 5.2). Then open the serial monitor:
- VS Code: the **plug** icon (PlatformIO: Serial Monitor) in the bottom status bar.
- Command line: `pio device monitor` (115200 baud comes from `platformio.ini`; add `-p PORT` if needed).

Opening the monitor resets the board. That is normal; each reset starts a new `session_N.csv`.

### 7.1 Expected serial lines

These come from `src/main.cpp`, `src/web/web_ui.cpp` and `src/report/telegram_report.cpp`. Order and timing vary slightly.

| Line | Meaning |
|---|---|
| `Boot: reset reason 1 (1=power-on 3=sw 4=panic 5/6/7=watchdog 9=brownout)` | Booted. 1 = power-on. |
| `DPF Sentinel \| board=freenove \| storage=SD card \| wifi=MyPhone \| telegram=on` | Banner. `storage=SD card` is the **setting**, not the result (see 7.2). `wifi=` shows slot 1, or `none configured`. |
| `Logging to SD as session_N.csv (+ raw_N.log)` | **The SD card works.** |
| `Logging to flash as session_N.csv (+ raw_N.log)` | The SD card was **not** found. See 7.2. |
| `Web UI: join Wi-Fi "DPF-Sentinel" (password …), then open http://192.168.4.1` | The logger's own hotspot is up. It prints your AP password. |
| `Telegram report: daily at 17:30 local time` | Token found, Telegram active. |
| `Telegram report: off (no bot token set)` | `TG_BOT_TOKEN` is empty. Go back to Stage 5. |
| `[boot] setup finished` | Start-up done. |
| `Connecting to BLE ELM327 adapter...` then `Scanning for a BLE OBD adapter...` | Looking for the adapter. |
| `No BLE adapter found — is it plugged into the OBD port with ignition on…` and later `Adapter link down — reconnecting...` every 5 s | **Normal on the bench**: there is no car. |
| `Wi-Fi "MyPhone" joined: http://172.20.10.2  (or http://obd-logger.local)` | Joined your hotspot (address will differ). |
| `Clock set from ntp: 1760000000 (unix)` | Time came from the internet. |
| `Alert sent` | A Telegram message went out. |
| `Wi-Fi not in range — hotspot only` | Slot 1 was not found within 20 s. It tries again every 10 minutes while the engine is off. |
| `WIFI MyPhone rssi=-48 dBm` / `WIFI not connected` | WiFi status, every 5 s. |

### 7.2 Is it really logging to the SD card?

The SD pins in `src/config.h` (CLK 39, CMD 38, D0 40, 1-bit mode) come from Freenove's published layout and are **not yet
verified on this unit**. You can tell what happened from three places:
1. The line `Logging to SD as …` (good) or `Logging to flash as …` (card not found).
2. Each data line ends with `| SD wf=0 rm=0 …` or `| flash wf=…`.
3. The web page footer (Stage 8) shows `SD` or `flash`.

Note: the banner's `storage=SD card` only means "SD is switched on in the settings". It prints the same even when the
card is not found. Trust the `Logging to …` line.

If it says **flash**:
1. Power off. Re-seat the card. Check it is FAT32 (Stage 1), not exFAT. Try another card, ideally 32 GB or smaller.
2. Still flash: the pins may be wrong for your board revision. Check the SD card pins in Freenove's own tutorial for
   this kit (their SD card example sketch defines them) and compare with `SDMMC_CLK_PIN`, `SDMMC_CMD_PIN`,
   `SDMMC_D0_PIN` in `src/config.h`. Change them only if Freenove's differ, then flash again.
3. Please report what you found (serial output plus board photo) so the docs can be corrected.

Meanwhile the logger still works on internal flash: about 30 minutes of space, oldest sessions deleted first
([log-format.md](log-format.md)).

### 7.3 Proof of life on Telegram

Within about a minute of the `joined` line you get a Telegram message from your bot:
```
Wi-Fi joined: MyPhone
IP 172.20.10.2
http://172.20.10.2
```
That one message proves the whole chain: WiFi, internet, clock, token and chat id. (Messages wait until the clock is
set, which happens automatically right after joining.) You get one of these every time the logger joins a network,
so in the car it doubles as "the logger is alive".

Also try sending **`/status`** to your bot. The logger checks for it every few seconds while online and replies
(only to your chat id). On the bench it answers `Car off or adapter out of range right now`. In the car it replies
with live soot, pressure, rpm, speed, exhaust temperature and battery voltage, plus an estimate of miles to the next
regen based on this car's measured rates.

### 7.4 Test the regen alert: /api/testalert

The address `/api/testalert` queues a fake alert so you can see what a regen alert looks like without waiting for one
(`handleTestAlert()` in `src/web/web_ui.cpp`). It sends:
```
TEST alert: the regen alert path works.
A real one reads: DPF regeneration started, with soot, miles since the last regen, and exhaust temperature.
```
Open it from any device that can reach the board, for example a laptop on the same hotspot:
`http://172.20.10.2/api/testalert` (the IP from the "Wi-Fi joined" message), or `http://192.168.4.1/api/testalert`
while joined to the `DPF-Sentinel` hotspot. The page only answers `{"queued":true}`. The message itself goes out
through the board's **own** internet connection, so it arrives only if the board has joined your hotspot or home WiFi.

Other addresses on the same web server:

| Address | Does |
|---|---|
| `/` | The live dashboard (Stage 8) |
| `/api/live` | Live values as JSON, including the next-regen estimate and `odometer_offset_mi` |
| `/api/odometer?dash=85000` | Sets the dash-mileage offset from your dash reading (engine on, adapter connected). See [Stage 9](#dash-mileage-and-the-next-regen-estimate) |
| `/api/time?ms=<unix ms>` | Sets the clock (the web page does this for you) |
| `/api/send` | Send the daily report **now**, with all unsent files |
| `/api/report` | Shows when the last report went and its result. `/api/report?reset=1` forgets what was sent (everything is re-sent next time) |
| `/api/statusnow` | Sends one status message now, **only if** `TG_STATUS_ENABLED` is 1 in `config.h` (it is 0 by default, and then this does nothing even though it answers `queued`) |
| `/api/files`, `/dl?f=session_N.csv` | List and download log files |

### 7.5 Serial command keys

With the serial monitor open, type a single letter (no Enter needed). From `serialCommands()` in `src/main.cpp`:

| Key | Does |
|---|---|
| `p` | Prints the last ~240 rows of the **previous** session (opening the monitor reboots the board and starts a new session, so the previous one is usually the drive you want) |
| `d` | Dumps **every** log file to the screen. Slow if there are many. |
| `O` (capital) | "I just changed the oil": records the current odometer as the oil-change reference and resets the regens-since-oil counter. Only works while the car is connected. |
| `e` | **Erases all log files** in `/obd` immediately, no confirmation. Reboot afterwards. Copy your logs off first. |
| `r` `l` `a` `s` `1` `2` | Sound tests for the screen version. On this board they only print a line. |

**You should now see:** `Logging to SD as session_N.csv`, the `Wi-Fi joined` message on Telegram, a reply to `/status`,
and the TEST alert. Your logger is ready for the car.

---

## 8. In the car

1. **Find the OBD port.** On most cars it is under the dashboard on the driver's side, near your knees. Your handbook
   shows where. Plug the adapter in firmly.
2. **Ignition on** (engine running is best). Many adapters light up when powered. If yours has a power button, switch it on.
3. **Phone hotspot on.**
4. **Power the board** from the switched USB socket (COM or USB socket both power it; use COM if you also want to watch
   it with a laptop).
5. Make sure **no phone app** (Car Scanner, Torque, etc.) is connected to the adapter. A BLE adapter accepts one
   connection at a time.

### How the logger finds the adapter

By default it scans for 10 seconds and connects to the strongest device whose name contains one of: OBD, ELM,
VLINK/V-LINK, VEEPEAK, IOS-, VGATE, KONNWEI, KW9, KW8, KW-, OBDLINK, MX+, LELink, CX (`src/obd/elm_client.cpp`). We have
not confirmed which name the B0DQV19QJF adapter advertises; most cheap BLE adapters use one of these.

**Pin your adapter (recommended once it works, or if it is not found).** This stops it picking a neighbour's adapter
in a car park and works even if the name is unusual. You need a laptop in the car for this:
1. In `src/config.h` set `#define BLE_SCAN_ONLY 1`. Upload. Open the serial monitor with the ignition on.
2. It prints `*** BLE_SCAN_ONLY ***` and a list every few seconds, e.g.
   `  aa:bb:cc:dd:ee:ff  name="IOS-Vlink"  rssi=-52   <-- looks like an OBD adapter`.
   **In this mode it logs nothing.**
3. Copy the address into `#define BLE_TARGET_ADDRESS "aa:bb:cc:dd:ee:ff"`.
4. Set `BLE_SCAN_ONLY` back to `0`. Upload again.

(If you do not have a laptop in the car, the auto-detect usually just works. Alternatively, `BLE_NAME_HINT` makes it
accept only devices whose name contains that text.)

### Confirming it works, without a screen

| Check | What you see |
|---|---|
| **Telegram** | `Wi-Fi joined: <your hotspot>` within about a minute of power-up. Then send `/status`: with the engine running it replies with real numbers (soot, DPF pressure, rpm…). |
| **Web page** | Only if the phone is **not** being the hotspot at that moment (most phones cannot be a hotspot and join another WiFi at once): join the WiFi **`DPF-Sentinel`** with your `WIFI_AP_PASS`, open **http://192.168.4.1** . Your phone may warn "no internet"; choose to stay connected. The pill at the top says `live` (engine data flowing), `adapter ok · engine off`, or `adapter not connected`. The footer shows `SD` or `flash`, and the clock source. |
| **/api/live** | `http://192.168.4.1/api/live` shows raw JSON. `"link":1` means the adapter is connected. `"values"` should hold numbers, not `null`, with the engine running. `"storage":"SD"` confirms the card. |
| **Serial** (laptop) | `Using adapter …`, `ELM327 adapter ready`, then one line per second like `t=42s rpm=812 spd=0 … soot=9.4g … \| SD wf=0 rm=0 wifi=-55dBm`. |

It may also be possible to open the board's page from the phone that is providing the hotspot, at the IP in the
"Wi-Fi joined" message. Whether this works depends on the phone; we have not verified it. The `/status` command always works.

**You should now see:** a `Wi-Fi joined` message, and a `/status` reply with live engine values. After a drive, the
card has a new `session_N.csv` with numbers in it.

---

## 9. The clock

The board has no battery clock of its own. It gets the time from, in order:

| Source | When | Log marker |
|---|---|---|
| **DS3231 RTC** (optional) | Immediately at power-up, if fitted ([wiring.md](wiring.md); on this board set `RTC_SDA_PIN 47` and `RTC_SCL_PIN 48` in `src/config.h`, `RTC_DS3231_ENABLED` is already 1) | `rtc` |
| **NTP** (internet time) | As soon as it joins your hotspot or home WiFi | `ntp` |
| **Your phone** | When you open http://192.168.4.1 while joined to `DPF-Sentinel`. The page sends your phone's time automatically; nothing to press. It is ignored if NTP already set the clock. | `phone` |

Rows logged before the time is known have an empty `unix_time`. A `TIME` line in the raw log records when it was set,
and `tools/date_session.py` fills the gaps afterwards (Stage 12). Log times are always UTC. The daily report time
and the times in alerts use UK time (GMT/BST, automatic) from `TZ_RULE` in `src/config.h`.

Telegram messages are only sent once the clock is set. With internet that happens within seconds of joining, so you
will not notice.

**You should now see:** a serial line `Clock set from ntp: …` (or `rtc` / `phone`), and the web page header shows the time.

### Dash mileage and the next-regen estimate

The web page (and the `/status` reply) shows an estimate of the miles to the next regeneration, worked out from how
fast soot has been building up and the soot level at which this car's regens have been starting. It is an estimate,
not an ECU value. Below **12 g** of soot the page shows **"not soon"** instead of a number, because when tested against
logged drives the estimate was typically 30 or more miles out at that stage.

The dash odometer and the ECU's odometer are often two different counters. To make the shown mileage match your dash,
open the web page with the engine running and the adapter connected, type the dash mileage into the box under
**Next regen & mileage** and tap **Save** (or open `/api/odometer?dash=85000` with your own figure). The offset is
stored on the board, survives power-off and needs no reflash. The logged CSV always keeps the ECU's raw value.
Details: [odometer-offset.md](odometer-offset.md).

---

## 10. What Telegram sends, and when

All of these need the board to be online (hotspot or home WiFi). While offline, up to **4** alerts wait in memory and go
out when it is next online. They are **lost if the board loses power first** (for example ignition off before you
had signal).

| Message | When | Setting |
|---|---|---|
| `Wi-Fi joined: <network>` + IP | Every time it joins a network | `TG_ALERT_WIFI 1` |
| `DPF regeneration started` + soot, miles since last regen, EGT, DPF pressure, time | The regen flag has been on for 3 rows (about 3 s) | `TG_ALERT_REGEN 1` |
| `DPF regen still running -- m:ss so far` + EGT, soot, pressure | **Every 30 seconds** during a regen | `TG_ALERT_REGEN 1` |
| `DPF regeneration finished` + duration, peak EGT, soot before -> after | The flag has been off for 3 rows | `TG_ALERT_REGEN 1` |
| `DPF regeneration was interrupted (engine stopped or signal lost)` | A regen was running and data stopped for over 2 minutes. Sent when data comes back (next drive). | `TG_ALERT_REGEN 1` |
| `Health: 3 regens in a row under 100 mi apart…` | Once per episode | `TG_ALERT_REGEN 1` |
| `Health: 15 regens since the last oil-change reference…` | Once per episode (reset with serial key `O`) | `TG_ALERT_REGEN 1` |
| `Health: warm-up to 80C took well over the usual time…` | Once per episode, after a few warm-ups are known | `TG_ALERT_REGEN 1` |
| Daily report: `OBD logger report, <date>`, number of files, latest soot/pressure/EGT/battery, a support link, then **each new log file attached** | Once per day, the first time the board is online **at or after 17:30 local time** (`TG_SEND_HOUR`:`TG_SEND_MIN`) | `TG_SEND_RAW 1` attaches the raw logs too; files under 3000 bytes are skipped (`TG_MIN_BYTES`) |
| `Status HH:MM: rpm …, speed …` | Every 5 min while the engine is running and coolant is at least 80 °C | Off by default: set `TG_STATUS_ENABLED 1` |
| Reply to `/status` | When you send `/status` | always on |

About the daily report:
- It only sends when the board is powered and online **after 17:30**. With switched power, that means a drive that
  is still going (with the hotspot on) after 17:30. If you never drive in the evening, change `TG_SEND_HOUR` /
  `TG_SEND_MIN` in `src/config.h` to a time you are usually driving, or open `/api/send` to send one on demand.
- It attaches every file it has not sent before, so a missed day is not lost; it just comes with the next report.
- If a send fails it tries again 10 minutes later.

The interrupted message also appears if you switch the engine off while a regen is running. Try to avoid that: when a
regen is running, finish the drive ([ix35-known-issues.md](ix35-known-issues.md)).

Do not read or answer these messages while driving.

---

## 11. Mounting and power in the car

The board comes **bare**: no case. A bare board touching metal, coins or a wet surface can short out.

Simple options (any is fine):
- A small **plastic** project box or a plastic sandwich-style tub with a hole for the cable. Fix it with double-sided
  foam tape or hook-and-loop (Velcro) strips.
- Wrap the underside in electrical tape and slide it into a padded glovebox or door pocket.
- Keep the microSD slot reachable if you plan to pull the card often.

Where:
- Somewhere **out of direct sun** (the top of the dashboard gets very hot), dry, and where it cannot slide around.
- **Not** near the pedals, gear lever or airbag panels. Route the cable so it cannot catch your feet or the pedals.
- Within a couple of metres of the OBD adapter. Bluetooth LE through the dashboard is usually fine at that distance.

Power:
- Use a USB socket that turns **off** with the ignition. Test it: ignition off, does the board's light go out? If the
  socket stays live, the board runs all the time and keeps talking to the adapter.
- 1 A or more is plenty.
- Starting the engine can make the voltage dip. If the board restarts during cranking, you will see a new session file
  and `reset reason 9` (brownout) in the serial output. That is harmless.
- The OBD adapter takes its power from the OBD port, and on many cars that port stays powered with the ignition off.
  If the car will stand for days, unplug the adapter.

### Optional status LEDs (GPIO 1 and 2)

| LED | Connect | Lights when |
|---|---|---|
| Regen | GPIO 1 -> resistor (220 to 330 ohm) -> LED long leg; LED short leg -> GND | a regen is running |
| Hot exhaust | GPIO 2 -> resistor -> LED long leg; LED short leg -> GND | EGT at or above `DPF_TEMP_WARN_THRESHOLD_C` (300 °C) |

Then in `src/config.h` set **both** `#define LED_REGEN_PIN 1` and `#define LED_TEMP_WARN_PIN 2`, and flash again. Set both:
the code switches on the pair together.

---

## 12. Getting the logs off

Files live in the `obd` folder on the card ([log-format.md](log-format.md)):
- `session_N.csv`: one row per second, the decoded values. Opens in Excel, LibreOffice or Google Sheets.
- `raw_N.log`: every raw adapter exchange, plus the `TIME` marker. Useful for debugging.

`N` goes up by one at every power-up (the counter is kept in the board's flash).

Four ways to get them:

| Way | How |
|---|---|
| **Card reader** (fastest) | Ignition **off** first, then pull the card. Rows are saved every second, so nothing is lost. Do not pull it while the board is running. |
| **Telegram** | The daily report attaches new files (Stage 10). |
| **Web page** | Join `DPF-Sentinel`, open http://192.168.4.1 , tap a file under **Log files**. Slower, fine for one drive. |
| **Script over Wi-Fi** | With your computer on the same network as the board: `python3 tools/fetch_board_logs.py` (uses `http://obd-logger.local`), or `python3 tools/fetch_board_logs.py http://192.168.4.1` on the `DPF-Sentinel` hotspot. It downloads every file it does not already have into `logs/cyd_<date>/` (git-ignored) and retries failed ones. Python 3, no extra packages. |

### Fill in missing times (date_session.py)

If the drive started before the clock was set, the first rows have an empty `unix_time`. Fill them from the `TIME`
marker in the matching raw log. You need Python 3 (macOS and Linux have it; on Windows install it from
https://www.python.org/downloads/ and tick "Add python.exe to PATH").

macOS / Linux:
```bash
python3 tools/date_session.py session_33.csv raw_33.log > session_33_dated.csv
```
Windows:
```powershell
python tools\date_session.py session_33.csv raw_33.log > session_33_dated.csv
```
It prints `# 120 rows dated from ntp marker` (or `no marker` if the clock was never set during that session, in which
case the file is unchanged). Rows that already had a time are left alone.

---

## 13. Updating the firmware later

1. Copy your logs off the card first (good habit).
2. Get the new code:
   - Git:
     ```bash
     git pull
     ```
     `include/secrets.h` is never touched by Git. If you changed `src/config.h` and `git pull` complains, run
     `git stash`, `git pull`, `git stash pop` to keep your changes.
   - ZIP: download the new ZIP, unzip it to a new folder, and copy your `include/secrets.h` into it. If you changed
     `src/config.h` (pinned adapter address, LEDs, RTC…), make the same changes in the new copy. Do not copy the old
     `config.h` over the new one; it may have new settings.
3. Upload as in [Stage 6](#6-build-and-flash) (`pio run -e freenove -t upload`). Then run the [Stage 7](#7-bench-test-at-home) checks again.

Uploading does not wipe the board's saved counters, so session numbers carry on.

> **Avoid "Erase Flash"** (PlatformIO's `erase` task, or `esptool erase_flash`) unless you really need it. It resets
> the session counter to 1, and new files would then **overwrite** `session_1.csv`, `session_2.csv`… already on the
> card. It also resets the Telegram "already sent" record and the health history. If you must erase, move the old
> files off the card first.

---

## 14. Troubleshooting

| Stage | Symptom | Fix |
|---|---|---|
| 1 | Card shows exFAT / logger says `flash` | Re-format as FAT32 (Stage 1). Prefer 32 GB or smaller. |
| 2 | PlatformIO IDE install stuck / fails on Linux | `sudo apt install python3-venv`, restart VS Code. |
| 2 | `pio` not recognised | Use the PlatformIO terminal inside VS Code, or `python -m platformio` / `python3 -m platformio`. Reopen the terminal after installing. |
| 6 | Upload works but the board behaves like a screen board / nothing on serial | You built the default `cyd35` environment. Upload with `-e freenove`. |
| 4 | No new port appears | Data cable (most likely), another USB port, then the driver for the chip next to the **COM** socket. |
| 4 | Port appears then vanishes (Ubuntu) | `sudo apt remove brltty`, replug. |
| 4 | `Permission denied` on `/dev/ttyUSB0` (Linux) | Add yourself to `dialout`, install the udev rules (Stage 2), log out and in. |
| 5 | Build stops: `TG_BOT_TOKEN must look like 12345678:AAbb…` | The token is missing its colon or was split. Paste the whole token. |
| 5 | Build stops: `TG_CHAT_ID is empty` | Fill in the chat id, or clear the token too. |
| 5 | `getUpdates` page shows `"result":[]` | Send another message to the bot and refresh. If the logger is already running with this token, it consumes the messages: power it off first. |
| 6 | Build fails on the first try with download errors | Check internet/proxy and run again; PlatformIO resumes. |
| 6 | `Failed to connect` / stuck at `Connecting…` | Hold BOOT, tap RESET, release BOOT, upload again. Close the serial monitor first. |
| 6 | `Could not open port` / port busy | A serial monitor (or Arduino IDE, Cura, etc.) has the port open. Close it. |
| 6 | Antivirus blocks esptool (Windows) | Allow `esptool` / Python in your antivirus. |
| 7 | Serial monitor shows nothing | You are on the **USB** socket; use **COM**. Press RESET. |
| 7 | Garbled characters | Baud must be 115200 (it is in `platformio.ini`; for other terminals set it yourself). |
| 7 | `Logging to flash as …` | SD not found: see [7.2](#72-is-it-really-logging-to-the-sd-card). |
| 7 | `Storage init failed — logging to Serial only` | Neither SD nor internal flash could be opened. Re-flash; if it persists, report it with the serial output. |
| 7 | `Telegram report: off (no bot token set)` | `include/secrets.h` missing, misnamed (`secrets.h.txt` on Windows), or token empty. Re-flash after fixing. |
| 7 | `WIFI not connected` forever | Wrong name/password (case-sensitive), 5 GHz network, or hotspot off. iPhone: Maximise Compatibility on. Slot 2 is only tried ~10 min later (Stage 5.2). |
| 7 | WiFi joined but no Telegram message | Token or chat id wrong (see [telegram-setup.md](telegram-setup.md)), you never pressed Start in the bot, or the network blocks internet (captive portal / guest WiFi). Serial shows `Alert sent` when it works. |
| 7 | `/api/testalert` answers `queued` but nothing arrives | The board itself has no internet. It must be joined to your hotspot or home WiFi, not only hosting `DPF-Sentinel`. |
| 7 | `/status` gets no reply | Same as above. The board answers only the chat id in `secrets.h`. |
| 8 | `No BLE adapter found` in the car | Ignition on, adapter seated and powered, no phone app connected to it. Use `BLE_SCAN_ONLY` to see what is in range, then pin `BLE_TARGET_ADDRESS`. |
| 8 | `No adapter-like names seen. Named devices in range:` | Your adapter's name is not in the list. Pin its address, or set `BLE_NAME_HINT`. |
| 8 | `BLE connect() failed` repeatedly | Move the board closer, unplug and replug the adapter, make sure no phone is paired and connected to it. |
| 8 | `Could not find a notify + write characteristic pair` | Unusual adapter. Set `BLE_DUMP_SERVICES 1` to list its services ([development.md](development.md)). |
| 8 | `Adapter init sequence failed` / `No decodable data — re-initialising adapter` | Ignition off or ECU asleep. Start the engine. It retries by itself. |
| 8 | Connected but most values empty | Normal with the engine off. On cars other than the ix35 1.7 CRDi, the PIDs are not supported ([pids.md](pids.md)). |
| 8 | No `Wi-Fi joined` in the car | Hotspot must be in **slot 1** and on **before** you start the car (Stage 5.2). Once the engine runs, it does not look for WiFi again. |
| 8 | Phone cannot open http://192.168.4.1 | Join `DPF-Sentinel` and stay connected despite "no internet". Turn mobile data off briefly if the browser keeps going to the internet. A phone being a hotspot usually cannot join another WiFi at the same time. |
| 9 | Times in the CSV empty | The clock was not set during that drive. Use the hotspot (NTP), open the web page once, or fit a DS3231. Then `date_session.py`. |
| 9 | Phone time ignored | NTP had already set the clock; that is the better source. |
| 10 | No daily report | The board must be online after 17:30 local. Change `TG_SEND_HOUR`/`TG_SEND_MIN`, or open `/api/send`. `/api/report` shows the last result. |
| 10 | Regen start alert missing, only "still running" or "finished" | The board was offline at the start and the 4-message queue was filled by "still running" messages (one every 30 s). Keep the hotspot on before starting the car. |
| 11 | New session file at every engine start | The voltage dip at cranking resets the board (`reset reason 9`). Harmless. A better USB adapter may help. |
| 11 | Board stays on with the ignition off | The USB socket is permanently live. Use a switched one. |
| 12 | Card unreadable on the PC | Pulled while running. Run a disk check (Windows: right-click > Properties > Tools > Check; macOS: Disk Utility > First Aid). |
| 13 | Session numbers restarted at 1 | Flash was erased. Old files on the card with the same names get overwritten: move them off. |

Still stuck? Copy the serial output from power-up to the problem (remove your passwords and token), and open an issue on
the GitHub repository.

---

## 15. Safety and legal

- **Read-only.** The logger only sends read requests (OBD mode 01 and Hyundai mode 21). It does not clear codes, start
  regens or change settings. It is not a diagnostic tool: do not base safety or repair decisions on it.
- **Do not tamper with the DPF.** The aim of this project is to keep a legal DPF healthy. Removing or disabling a DPF or
  EGR is illegal for road use in the UK and many other countries, fails the MOT and harms health.
- **Do not use your phone while driving.** Read Telegram messages when parked. Handling a phone while driving is illegal
  in the UK and many other places. Do not fiddle with the board, laptop or web page on the move.
- **Mount it safely.** Nothing loose near the pedals, steering column or airbags. Cables routed out of the way. No bare
  board against metal.
- **Battery.** An adapter left in the OBD port can slowly drain the battery when the car stands for days.
- **Your secrets.** The bot token is a password. If it leaks, `/revoke` it with @BotFather and put the new one in
  `secrets.h`. Your logs contain your mileage and when you drive; share them with care.
- **Warranty and insurance.** Plugging third-party devices into the OBD port is common, but check your warranty and
  insurance terms if in doubt.
- Full disclaimer: [disclaimer.md](disclaimer.md).
- Licence: GPL-3.0-or-later, provided without warranty. See [NOTICE.md](../NOTICE.md) and [LICENSE](../LICENSE).

If this saves you a DPF job, you can support the project on [Ko-fi](https://ko-fi.com/vitoi).
