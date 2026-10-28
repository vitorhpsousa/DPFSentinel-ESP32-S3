#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 the DPF Sentinel project
"""Read-only OBD-II discovery run for your car, against any serial-connected
ELM327-compatible adapter.

Identifies the car and protocol, lists supported PIDs, VIN/calibration,
which ECUs answer, DTCs (read only), samples the supported PIDs while
revving, then probes a list of community-sourced manufacturer-mode
candidates that sometimes carry DPF-related data. Every request and raw
reply is written to a log file. Nothing is written to the car: only
services 01, 03, 06, 07, 09, 0A, 22 and ATxx/STxx adapter commands are sent.

This is the first of three "adapt this project to another car" tools; see
tools/README.md (next to this file) for the workflow: this script first, to
find out what your car supports; then a logged session with the Car Scanner
app; then carscanner_parse.py + carscanner_solve.py to derive exact byte
offsets and scaling for whatever you found here.

It is independent of the ESP32-S3 firmware in this repository (which has no
discovery mode of its own). Run it from any computer with an ELM327/STN-type
adapter that shows up as a serial port: a USB adapter, or a classic-Bluetooth
(SPP) adapter paired to the computer. A Bluetooth LE-only adapter (like the
one the logger itself uses) is not a serial port and will not work here.

Dependency: pyserial (`pip install pyserial`). Everything else used here is
the Python standard library.

Run with ignition on, engine off first; when it prints REV, hold ~2000-3000
rpm for a few seconds, then let it idle. `--device` is the serial port, for
example `/dev/ttyUSB0` or `/dev/ttyACM0` (Linux), `/dev/cu.usbserial-110`
(macOS) or `COM3` (Windows).

    python3 discover.py --device /dev/ttyUSB0 [--sample-seconds 60]
"""
import argparse
import re
import sys
import time
from typing import List, Optional

import serial

# ---------------------------------------------------------------------------
# Minimal standalone ELM327 client + ISO-TP reassembly. No project-specific
# dependency beyond pyserial, so this script can run anywhere. See
# carscanner_parse.py in this same directory for a second, slightly
# different reassembly routine used when parsing a Car Scanner log instead
# of talking to the adapter live.
# ---------------------------------------------------------------------------


class ElmClient:
    """Thin wrapper over a serial connection to an ELM327-compatible adapter
    (plain-text command/response, no framing)."""

    def __init__(self, device: str, baud: int):
        self._ser = serial.Serial(device, baud, timeout=0, write_timeout=2.0)
        self._current_header = None
        self.last_raw = ""

    def close(self):
        self._ser.close()

    def send_command(self, cmd: str, timeout_s: float = 2.0) -> str:
        self._ser.reset_input_buffer()
        self._ser.write((cmd + "\r").encode("ascii"))

        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self._ser.read(64)
            if chunk:
                buf += chunk
                if b">" in chunk:
                    break
            else:
                time.sleep(0.01)
        text = buf.decode("ascii", errors="ignore")
        return text.replace(">", "").strip()

    def set_header(self, header: str) -> bool:
        """Selects the CAN transmit header (ATSH) — e.g. 7DF for functional
        mode 01 requests, 7E0 for a typical ECM's manufacturer mode 21/22.
        No-ops if it's already selected."""
        if header == self._current_header:
            return True
        resp = self.send_command("ATSH" + header)
        if "OK" not in resp:
            return False
        self._current_header = header
        return True

    def query_raw(self, request_hex: str, frames: str = "", timeout_s: float = 3.0) -> str:
        """Sends a request (with the ELM327 expected-frame-count digit
        appended so multi-frame replies return as soon as they're
        complete) and returns the adapter's raw text — frames separated by
        \\r, or an error string such as "NO DATA". Never filtered: the raw
        reply is what gets logged for later analysis."""
        resp = self.send_command(request_hex + frames, timeout_s=timeout_s)
        self.last_raw = resp
        return resp


_HEX = re.compile(r"^[0-9A-Fa-f]+$")


def extract_payload(text: str, rx_id: str) -> Optional[bytes]:
    """Reassembles an ISO-TP (CAN) payload from ELM327/STN output with
    headers on (ATH1) and spaces off (ATS0), e.g.

        7E8034104A2                       single frame  -> 41 04 A2
        7E8104B6103FFFFFFFF\\r7E821...     first + consecutive frames

    Returns the payload starting at the service ID (0x41.. / 0x61..), or
    None if the reply is missing, from another ECU, or incomplete.
    """
    frames: List[bytes] = []
    for line in text.replace("\n", "\r").split("\r"):
        line = line.strip().replace(" ", "").upper()
        if len(line) < 5 or (len(line) - 3) % 2 or not _HEX.match(line):
            continue
        if line[:3] != rx_id:
            continue
        frames.append(bytes.fromhex(line[3:]))
    if not frames:
        return None
    f0 = frames[0]
    if len(f0) < 2:
        return None
    pci = f0[0] >> 4
    if pci == 0:
        n = f0[0] & 0x0F
        return f0[1:1 + n] if len(f0) >= 1 + n and n else None
    if pci == 1:
        total = ((f0[0] & 0x0F) << 8) | f0[1]
        data = bytearray(f0[2:])
        for fr in frames[1:]:
            if fr and fr[0] >> 4 == 2:
                data += fr[1:]
        return bytes(data[:total]) if len(data) >= total else None
    return None


# ---------------------------------------------------------------------------
# Discovery script proper (same shape regardless of adapter/OS/platform).
# ---------------------------------------------------------------------------

DPF_PROBES = [  # community-sourced UDS probes, UNVERIFIED; header 7E0 unless noted
    "223274", "223275", "223276", "223277", "223278", "223279",   # forum thread, 1.7-litre diesel
    "22336A", "2220F4", "223039", "2220FA", "2220F5",             # forum thread, 1.6-litre diesel
    "220023", "221152", "220005", "220010",                       # rail, EGR, coolant, MAF
    "22F190", "22F187", "22F189", "22F18C",                       # UDS identity DIDs
]
MODULES = ["7E0", "7E1", "7E2", "7E3", "7E4", "7E5", "7E6", "7E7"]


class Log:
    def __init__(self, path):
        self.f = open(path, "w", buffering=1)
        self.t0 = time.monotonic()
        print("logging to", path)

    def line(self, s=""):
        t = f"{time.monotonic() - self.t0:8.2f}"
        self.f.write(f"{t}  {s}\n")
        print(s)


def rx_for(header):
    return "%03X" % (int(header, 16) + 8)


def ask(elm, log, header, req, frames="", timeout=3.0):
    if header:
        elm.set_header(header)
    raw = elm.query_raw(req, frames, timeout_s=timeout)
    log.line(f"[{header or '-'}] > {req}{frames}")
    for ln in raw.replace("\n", "\r").split("\r"):
        if ln.strip():
            log.line(f"      < {ln.strip()}")
    return raw


def bitmap_supported(payload, base):
    """payload = 41 PP b0 b1 b2 b3 -> list of supported PIDs in that block."""
    out = []
    if not payload or len(payload) < 6:
        return out
    bits = int.from_bytes(payload[2:6], "big")
    for i in range(32):
        if bits & (1 << (31 - i)):
            out.append(base + i + 1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", required=True,
                    help="serial port the adapter is on: e.g. /dev/ttyUSB0 or /dev/ttyACM0 "
                         "(Linux), /dev/cu.usbserial-110 (macOS), COM3 (Windows)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--sample-seconds", type=int, default=60)
    ap.add_argument("--out", default=None)
    ap.add_argument("--unattended", action="store_true",
                    help="no screen needed: keep retrying until the adapter and car answer, "
                         "wait for the engine to start, then sample")
    ap.add_argument("--wait-minutes", type=int, default=30,
                    help="unattended: how long to keep waiting for adapter/ignition/engine")
    a = ap.parse_args()

    out = a.out or time.strftime("car_discovery_%Y%m%d_%H%M%S.log")
    log = Log(out)
    deadline = time.monotonic() + a.wait_minutes * 60

    log.line("== STAGE 1: adapter and protocol")
    while True:
        elm = None
        try:
            elm = ElmClient(a.device, a.baud)
            elm.send_command("ATZ", timeout_s=3.0)
            for cmd in ("ATE0", "ATL0", "ATH1", "ATS0", "ATCAF1", "ATSP0", "ATST96"):
                elm.send_command(cmd)
            for cmd in ("ATI", "STI", "ATDESC", "ATRV"):
                log.line(f"{cmd}: {elm.send_command(cmd)}")
            # First real request triggers protocol search (can take ~10 s).
            raw = ask(elm, log, "7DF", "0100", "", timeout=15.0)
            log.line("ATDPN: " + elm.send_command("ATDPN"))
            log.line("ATDP: " + elm.send_command("ATDP"))
            if not ("NO DATA" in raw or "UNABLE" in raw or not raw):
                break
            log.line("No answer to 0100 yet (ignition on?).")
        except Exception as e:  # serial errors while the link is not up yet
            log.line(f"Adapter not reachable yet: {e.__class__.__name__}")
        if elm is not None:
            try:
                elm.close()
            except Exception:
                pass
        if not a.unattended or time.monotonic() > deadline:
            log.line("!! Gave up: no adapter/car answer. Check power, range, pairing, ignition.")
            return 1
        time.sleep(10)

    log.line("== STAGE 2: supported PID bitmaps (mode 01, ECM 7E0)")
    supported = []
    base = 0
    while True:
        raw = ask(elm, log, "7E0", "01%02X" % base, "1", timeout=5.0)
        p = extract_payload(raw, "7E8")
        sup = bitmap_supported(p, base)
        supported += sup
        log.line("  supported: " + " ".join("%02X" % x for x in sup))
        if not sup or (base + 0x20) not in sup:
            break
        base += 0x20
        if base > 0xC0:
            break
    log.line("ALL SUPPORTED: " + " ".join("%02X" % x for x in supported))

    log.line("== STAGE 3: identity (mode 09)")
    for req in ("0900", "0902", "0904", "090A"):
        raw = ask(elm, log, "7E0", req, "", timeout=5.0)
        p = extract_payload(raw, "7E8")
        if p and req == "0902" and len(p) > 3:
            log.line("  VIN: " + bytes(p[3:]).decode("ascii", "replace"))
        if p and req == "0904" and len(p) > 3:
            log.line("  CAL ID: " + bytes(p[3:]).decode("ascii", "replace"))
    ask(elm, log, "7E0", "0151", "1")  # fuel type (04 = diesel)

    log.line("== STAGE 4: module census (0100 on each 7E0-7E7, headers on)")
    for h in MODULES:
        raw = ask(elm, log, h, "0100", "", timeout=1.5)
        alive = extract_payload(raw, rx_for(h)) is not None
        log.line(f"  {h}->{rx_for(h)}: {'ANSWERS' if alive else '-'}")
    ask(elm, log, "7DF", "0100", "", timeout=3.0)  # functional: shows every responder

    log.line("== STAGE 5: fault codes (read only)")
    for req in ("03", "07", "0A"):
        ask(elm, log, "7E0", req, "", timeout=4.0)
    log.line("  (raw only; decode P/C/B/U codes from the bytes above)")

    log.line("== STAGE 6: mode 06 raw (a few monitors)")
    for req in ("0600", "0601", "0621"):
        ask(elm, log, "7E0", req, "", timeout=4.0)

    log.line("== STAGE 7: one pass of every supported mode 01 PID")
    live = [x for x in supported if x % 0x20 != 0]
    for pid in live:
        ask(elm, log, "7E0", "01%02X" % pid, "1", timeout=2.5)

    if a.unattended:
        log.line("== Waiting for the engine to start (rpm > 400)")
        while time.monotonic() < deadline:
            p = extract_payload(ask(elm, log, "7E0", "010C", "1", timeout=2.0), "7E8")
            if p and len(p) >= 4 and ((p[2] * 256 + p[3]) / 4.0) > 400:
                break
            time.sleep(2)
        else:
            log.line("Engine never started within the wait time; sampling anyway.")
        log.line("Engine running: rev it a few times in the next minute, then let it idle.")
    log.line("== STAGE 8: REV window (engine running). Hold 2000-3000 rpm now, then idle.")
    sample = [x for x in (0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x23, 0x42, 0x5C, 0x5E, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F)
              if x in live]
    end = time.monotonic() + a.sample_seconds
    while time.monotonic() < end:
        for pid in sample:
            ask(elm, log, "7E0", "01%02X" % pid, "1", timeout=1.5)

    log.line("== STAGE 9: DPF / enhanced candidates (mode 22 on 7E0; 7F 22 11 = unsupported)")
    for req in DPF_PROBES:
        ask(elm, log, "7E0", req, "", timeout=3.0)

    log.line("== DONE. Copy this log off the machine and keep it with the Car Scanner capture.")
    elm.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
