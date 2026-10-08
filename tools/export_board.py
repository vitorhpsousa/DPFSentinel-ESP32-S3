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
s = re.sub(r"\n{3,}", "\n\n", s)
if board == "freenove": s = s.replace("-<audio/> -<board/> -<input/> -<ui/>", "-<audio/> -<board/> -<input/> -<ui/>")
open(p, "w").write(s.strip() + "\n")

# config.h: keep only this board's pin block
p = os.path.join(out, "src", "config.h"); s = open(p).read()
m = re.search(r"#if defined\(BOARD_CYD35\)(.*?)#elif defined\(BOARD_FREENOVE\)(.*?)#else", s, flags=re.S)
keep = m.group(1) if board == "cyd35" else m.group(2)
s = s[:m.start()] + "#define BOARD_%s 1   // this repo supports one board only\n" % board.upper() + "#define HAS_DISPLAY %d" % (1 if board == "cyd35" else 0) + re.sub(r"\s*#define HAS_DISPLAY \d", "", keep).rstrip() + "\n" + s[m.end():].replace('  #error "Select a board: build with env cyd35 or freenove"\n#endif', "", 1)
open(p, "w").write(s)

# README / docs wording
names = {"cyd35": ("DPF Sentinel — CYD 3.5\" (ESP32-S3)", "CYD ESP32-S3 3.5\" ST77922 touch screen board"),
         "freenove": ("DPF Sentinel — ESP32-S3 N16R8 (no screen)", "ESP32-S3-WROOM-1 N16R8 board, no screen")}
p = os.path.join(out, "README.md"); s = open(p).read()
s = re.sub(r"^# .*$", "# " + names[board][0], s, count=1, flags=re.M)
s = re.sub(r"^\| (Screen logger|Headless logger) \|.*(\n)", lambda m: m.group(0) if ((board == "cyd35") == (m.group(1) == "Screen logger")) else "", s, flags=re.M)
s = s.replace("-e cyd35", "").replace("-e freenove", "").replace("| `cyd35` |", "|").replace("| `freenove` |", "|").replace("| Env |", "|")
open(p, "w").write(s)
for f in ("docs/flashing-windows.md", "docs/development.md", "docs/hardware.md"):
    p = os.path.join(out, f)
    if os.path.exists(p):
        t = open(p).read()
        t = re.sub(r"^.*(pio run -e %s|%s).*$\n" % (other, other), "", t, flags=re.M)
        t = t.replace(" -e %s" % board, "")
        open(p, "w").write(t)
print("exported", board, "->", out)
