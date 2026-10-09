#!/usr/bin/env python3
"""Export a single-board copy of this repo.   python3 tools/export_board.py cyd35|freenove OUT_DIR
Copies everything except the other board's files and rewrites platformio.ini / README for one board."""
import os, re, shutil, sys

board, out = sys.argv[1], sys.argv[2]
src = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
other = "freenove" if board == "cyd35" else "cyd35"
drop = {".git", ".pio", "__pycache__", "secrets.h", "export_board.py"}
if board == "freenove":
    drop |= {"audio", "board", "input", "ui", "gen_fonts.py", "fonts", "cyd35-board-notes.md", "cyd35-hardware-notes.md"}
else:
    drop |= {"wiring.md"} if False else set()
if os.path.exists(out): shutil.rmtree(out)
shutil.copytree(src, out, ignore=lambda d, names: [n for n in names if n in drop or n.endswith(".pyc")])

# platformio.ini: one board env, set as default
p = os.path.join(out, "platformio.ini"); s = open(p).read()
s = re.sub(r"\[env:%s\].*?(?=\n\[|\Z)" % other, "", s, flags=re.S)
s = re.sub(r"default_envs = \w+", "default_envs = " + board, s)
s = re.sub(r"^;   pio run -e %s .*\n" % other, "", s, flags=re.M)
s = s.replace("One codebase, two boards:", "One board:")
s = re.sub(r"\n{3,}", "\n\n", s)
if board == "freenove": s = s.replace("-<audio/> -<board/> -<input/> -<ui/>", "-<audio/> -<board/> -<input/> -<ui/>")
open(p, "w").write(s.strip() + "\n")

# config.h: keep only this board's pin block
p = os.path.join(out, "src", "config.h"); s = open(p).read()
m = re.search(r"#if defined\(BOARD_CYD35\)(.*?)#elif defined\(BOARD_FREENOVE\)(.*?)#else", s, flags=re.S)
keep = m.group(1) if board == "cyd35" else m.group(2)
keep = re.sub(r"^  ", "", keep, flags=re.M).strip("\n")
s = s[:m.start()] + keep + "\n" + s[m.end():].replace('  #error "Select a board: build with env cyd35 or freenove"\n#endif', "", 1)
s = s.replace("// Board selection comes from the PlatformIO env (-D BOARD_CYD35=1 or -D BOARD_FREENOVE=1).", "// This repo supports one board; the board macro is set by platformio.ini.")
open(p, "w").write(s)

# README / docs wording
names = {"cyd35": ("DPF Sentinel — CYD 3.5\" (ESP32-S3)", "CYD ESP32-S3 3.5\" ST77922 touch screen board"),
         "freenove": ("DPF Sentinel — ESP32-S3 N16R8 (no screen)", "ESP32-S3-WROOM-1 N16R8 board, no screen")}
p = os.path.join(out, "README.md"); s = open(p).read()
s = re.sub(r"^# .*$", "# " + names[board][0], s, count=1, flags=re.M)
s = re.sub(r"^\| (Screen logger|Headless logger) \|.*(\n)", lambda m: m.group(0) if ((board == "cyd35") == (m.group(1) == "Screen logger")) else "", s, flags=re.M)
s = s.replace("-e cyd35", "").replace("-e freenove", "").replace("| `cyd35` |", "|").replace("| `freenove` |", "|").replace("| Env |", "|")
# drop the "Env" column of the hardware table
s = re.sub(r"^(\|.*?)\s*\|\s*(Env|---|`\w+`|both)\s*\|$", r"\1 |", s, flags=re.M)
s = s.replace("|---|--- |", "|---|---|")
open(p, "w").write(s)
for f in ("docs/flashing-windows.md", "docs/development.md", "docs/hardware.md"):
    p = os.path.join(out, f)
    if os.path.exists(p):
        t = open(p).read()
        t = re.sub(r"^.*(pio run -e %s|%s).*$\n" % (other, other), "", t, flags=re.M)
        t = t.replace(" -e %s" % board, "")
        open(p, "w").write(t)

# ---- board-specific docs (written out, so the end user never sees the other board) ----
def sub(path, fn):
    q = os.path.join(out, path)
    if os.path.exists(q):
        new = fn(open(q).read())
        open(q, "w").write(new)

if board == "freenove":
    hw = """# Hardware

## Board: ESP32-S3-WROOM-1 N16R8 (no screen)
Freenove ESP32-S3 WROOM kit (B0F48DV38M): 16 MB flash, 8 MB octal PSRAM, USB-C, microSD slot. The camera is unused.
microSD on SD_MMC **1-bit**: CLK 39, CMD 38, D0 40 (D1-D3 unused). These pins come from Freenove's published layout
and are **not yet verified on this unit**: the first boot should print `storage=SD card`; if it says flash, check the
pins in `src/config.h`. GPIO 1 and 2 are free for status LEDs (`LED_REGEN_PIN`, `LED_TEMP_WARN_PIN`).
"""
    wiring = """# DS3231 RTC (optional)

Without it the clock comes from NTP or your phone each boot. With it the board has the right time immediately.

| DS3231 | Board |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 47 |
| SCL | GPIO 48 |

Then in `src/config.h` set `RTC_DS3231_ENABLED 1`, `RTC_SDA_PIN 47`, `RTC_SCL_PIN 48`. Address 0x68. The RTC holds UTC and
is rewritten on every NTP/phone sync. ZS-042-style modules try to charge a non-rechargeable CR2032: remove the charge
resistor/diode or use a LIR2032. Confirm GPIO 47/48 are free on your board before soldering.
"""
    usb = """## 3. Connect the board and find its port
The board has two USB-C sockets. Use the one marked **COM** (or UART) for everything: flashing and reading messages.
Then `pio device list` should show a `COMx` entry (Device Manager > Ports shows the same). If nothing appears, try
another data-capable cable, or install the CH340/CP210x driver for the chip next to that socket. The other socket
(marked USB) can power the board but prints nothing.

"""
    sd = "is missing, not FAT32, or the SD pins in `src/config.h` need checking."
    notice = "Copyright the DPF Sentinel authors. Licensed GPL-3.0-or-later (see LICENSE).\n\nThe Telegram root certificate is a public CA certificate.\n"
    name = "ESP32-S3 N16R8"
else:
    hw = """# Hardware

## Board: CYD ESP32-S3 3.5" touch screen
ESP32-S3, 16 MB flash, 8 MB PSRAM, ST77922 QSPI 320x480, touch, ES8311 speaker, microSD on SD_MMC 4-bit
(CLK5 CMD4 D0-D3 6/7/2/3). All pins were proven from the factory firmware and live probing:
[cyd35-hardware-notes.md](cyd35-hardware-notes.md), [cyd35-board-notes.md](cyd35-board-notes.md).
Back up the factory flash before first flashing (see [flashing-windows.md](flashing-windows.md)).
"""
    wiring = """# DS3231 RTC (optional)

Without it the clock comes from NTP or your phone each boot. With it the board has the right time immediately.

| DS3231 | Board |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 38 (shared with touch) |
| SCL | GPIO 39 (shared with touch) |

Then in `src/config.h` set `RTC_DS3231_ENABLED 1`; leave `RTC_SDA_PIN/RTC_SCL_PIN` at -1 (the touch driver already started
that I2C bus). Address 0x68. The RTC holds UTC and is rewritten on every NTP/phone sync. ZS-042-style modules try to charge
a non-rechargeable CR2032: remove the charge resistor/diode or use a LIR2032.
"""
    usb = """## 3. Connect the board and find its port
The board has two USB-C sockets. Use the one marked **COM** (or UART) for everything: flashing and reading messages.
Then `pio device list` should show a `COMx` entry (Device Manager > Ports shows the same). If nothing appears, try
another data-capable cable, or install the CH340/CP210x driver for the chip next to that socket. The other socket
(marked USB) can power the board but prints nothing.

"""
    sd = "is missing or not FAT32."
    notice = open(os.path.join(src, "NOTICE.md")).read()
    name = "CYD 3.5in"
sub("docs/hardware.md", lambda t: hw)
sub("docs/wiring.md", lambda t: wiring)
sub("NOTICE.md", lambda t: notice if board == "cyd35" else "# Notices\n" + notice)
def win(t):
    t = re.sub(r"## 3\. Connect.*?(?=## 5\.)", lambda m: usb, t, flags=re.S)
    t = re.sub(r"is missing, not FAT32, or \(Freenove\).*?checking\.", sd, t)
    t = t.replace("DPFSentinel-ESP32-S3", "DPFSentinel-" + ("CYD35" if board == "cyd35" else "S3-N16R8"))
    return t
sub("docs/flashing-windows.md", win)
sub("docs/development.md", lambda t: t if board == "cyd35" else re.sub(r"; CYD only:.*\n", ".\n", t))
sub("src/config.h", lambda t: t.replace("#define RTC_DS3231_ENABLED 1", "#define RTC_DS3231_ENABLED 0   // set to 1 and set the two pins below once a DS3231 is wired (docs/wiring.md)") if board == "freenove" else t)
sub("src/config.h", lambda t: t.replace("; Freenove: set e.g. 47", "").replace("(CYD: shares the touch bus 38/39)", "(shares the touch bus 38/39)") if board == "cyd35" else t.replace("-1 = board default (CYD: shares the touch bus 38/39); Freenove: set e.g. 47", "set to your SDA pin, e.g. 47"))
sub("README.md", lambda t: t.replace("+ cyd-only board/ input/ audio/ ui/", "") if board == "freenove" else t.replace("cyd-only ", "").replace("+ board/", "board/"))
sub("README.md", lambda t: re.sub(r"pio run\s+-t upload\s+# or:.*", "pio run -t upload                                # build and flash", t))
sub("README.md", lambda t: t.replace("On the screen board it also shows a\ncolour-coded soot panel. ", "") if board == "freenove" else t)
sub("README.md", lambda t: t.replace("git clone https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3.git && cd DPFSentinel-ESP32-S3", "git clone https://github.com/vitorhpsousa/DPFSentinel-%s.git && cd DPFSentinel-%s" % (("CYD35",)*2 if board == "cyd35" else ("S3-N16R8",)*2)))
sub("platformio.ini", lambda t: re.sub(r"^;   pio run -e freenove.*\n", ";   pio run -t upload   build and flash (Freenove ESP32-S3 N16R8, no screen, SD only)\n", t, flags=re.M) if board == "freenove" else t)
print("exported", board, "->", out)
