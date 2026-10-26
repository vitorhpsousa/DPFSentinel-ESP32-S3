#!/usr/bin/env python3
"""Fetch every log file off a running board over its web UI (stdlib only, no
pip install needed). Skips files already saved locally with a matching size,
downloads the rest one at a time with retries (the board's web server drops
the connection if you hit it too hard/in parallel), and reports anything that
didn't come down clean so you don't trust a corrupt file silently.

Usage:
    python3 tools/fetch_board_logs.py [BOARD_URL] [OUT_DIR]

    BOARD_URL defaults to http://obd-logger.local (mDNS name the firmware
    registers); pass the IP (e.g. http://192.168.1.50) if that doesn't resolve.
    OUT_DIR defaults to logs/cyd_<today's date> next to this repo.

Looks for existing copies in every logs/cyd_*/ and logs/pi-*/ folder (one
directory up from this repo, i.e. ../OBD/logs/ if you're set up the usual
way) — edit SEARCH_ROOTS below if your logs live somewhere else.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://obd-logger.local"
RETRIES = 3
RETRY_DELAY_S = 2
BETWEEN_FILES_S = 0.3  # be gentle with the board's single-threaded web server

# Files known to crash the board when read (corrupted SD cluster chain: the directory
# entry gives a plausible size but the data loops into unwritten sectors, and neither
# the serial dump nor the HTTP download handler yields while reading it, which trips
# the task watchdog and reboots the board). Add to this list if another file does the
# same — the symptom is the board restarting partway through a fetch run.
SKIP_FILES = {"session_104.csv"}


def find_existing(search_roots):
    """name -> (path, size) for every file already saved locally."""
    found = {}
    for root in search_roots:
        if not os.path.isdir(root):
            continue
        for d in sorted(os.listdir(root)):
            full = os.path.join(root, d)
            if not os.path.isdir(full):
                continue
            for name in os.listdir(full):
                p = os.path.join(full, name)
                if os.path.isfile(p):
                    found[name] = (p, os.path.getsize(p))
    return found


def fetch(url, timeout=30):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # repo root
    obd_logs = os.path.join(os.path.dirname(here), "OBD", "logs")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(obd_logs, time.strftime("cyd_%Y-%m-%d"))
    os.makedirs(out_dir, exist_ok=True)

    print(f"board: {base}")
    listing = None
    # The board's web server handles one request at a time (its own dashboard page polls
    # it too), so a single timed-out attempt doesn't mean it's down — retry with backoff
    # before giving up, same as every individual file download below.
    for attempt in range(1, RETRIES + 1):
        try:
            listing = json.loads(fetch(base + "/api/files", timeout=25))
            break
        except Exception as e:
            print(f"  /api/files: {e} (attempt {attempt}/{RETRIES})")
            if attempt < RETRIES:
                time.sleep(RETRY_DELAY_S * attempt)  # back off a bit more each time
    if listing is None:
        sys.exit(f"could not reach {base}/api/files after {RETRIES} tries.\n"
                 f"(check the board's IP — it prints it to serial on boot — and that it's on this network; "
                 f"if its own dashboard page won't load in a browser either, it needs a power cycle)")

    existing = find_existing([obd_logs])
    total = len(listing)
    got, reused, failed, skipped = 0, 0, [], []

    for i, f in enumerate(listing, 1):
        name, size = f["name"], f["size"]
        if name in SKIP_FILES:
            print(f"[{i}/{total}] {name}: SKIPPED (known to crash the board on read)")
            skipped.append(name)
            continue
        dest = os.path.join(out_dir, name)
        if os.path.exists(dest) and os.path.getsize(dest) == size:
            reused += 1
            continue
        if name in existing and existing[name][1] == size:
            # already have an identical copy from an earlier fetch elsewhere; just reuse it
            data = open(existing[name][0], "rb").read()
            open(dest, "wb").write(data)
            reused += 1
            continue

        ok = False
        for attempt in range(1, RETRIES + 1):
            try:
                data = fetch(f"{base}/dl?f={name}", timeout=60)
                if len(data) == size:
                    open(dest, "wb").write(data)
                    got += 1
                    ok = True
                    break
                print(f"  [{i}/{total}] {name}: got {len(data)} bytes, board says {size} "
                      f"(attempt {attempt}/{RETRIES})")
            except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
                print(f"  [{i}/{total}] {name}: {e} (attempt {attempt}/{RETRIES})")
            time.sleep(RETRY_DELAY_S)
        if not ok:
            failed.append(name)
        print(f"[{i}/{total}] {name} ({size} bytes){' OK' if ok else ' FAILED'}")
        time.sleep(BETWEEN_FILES_S)

    print(f"\n{out_dir}")
    print(f"downloaded {got}, reused {reused} already-local, {len(failed)} failed, {len(skipped)} skipped")
    if skipped:
        print("SKIPPED (edit SKIP_FILES at the top of this script if you really need one of these):")
        for n in skipped:
            print(" -", n)
    if failed:
        print("FAILED (board may have a corrupt file — check it directly with the board's "
              "'p' serial command, or just leave it, it won't block the others):")
        for n in failed:
            print(" -", n)


if __name__ == "__main__":
    main()
