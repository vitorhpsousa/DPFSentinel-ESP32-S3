# Tools

Plain Python 3 helpers. None of them talk to the car except `discover.py`, and that one is read-only.

| Script | What for | Needs |
|---|---|---|
| `date_session.py` | Back-fill empty `unix_time` cells from the `TIME` marker in the raw log ([log-format.md](../docs/log-format.md)) | stdlib |
| `fetch_board_logs.py` | Download every log file from a running board over Wi-Fi, skipping ones you already have | stdlib |
| `export_board.py` | Export a single-board copy of the source tree | stdlib |
| `gen_fonts.py` | Regenerate the anti-aliased screen fonts from `fonts/` | Pillow |
| `discover.py` | First look at a **different** car: what it supports | `pyserial` |
| `carscanner_parse.py`, `carscanner_solve.py` | Derive byte offsets and scaling from a Car Scanner capture | `numpy` (solve) |

## Adapting to another car

None of this is needed on the verified car (2014 Hyundai ix35 1.7 CRDi). These three scripts are for someone adapting
the logger to a **different** car, where [docs/pids.md](../docs/pids.md) does not apply and the request bytes, offsets
and scaling have to be found again. Full write-up: [docs/other-cars.md](../docs/other-cars.md).

All three are read-only: they only ask for data (OBD services 01, 03, 06, 07, 09, 0A, 22 and `AT`/`ST` adapter
commands), or only read files. Nothing here writes to an ECU, clears codes or changes a diagnostic session.

### 1. `discover.py`: find out what your car supports

Run first, from a computer, against an ELM327/STN-type adapter that appears as a **serial port** (a USB adapter, or a
classic-Bluetooth adapter paired to the computer). The Bluetooth LE adapter the logger uses is not a serial port and
will not work here. Ignition on, engine off to start. It identifies the protocol, lists every supported standard PID,
reads VIN and calibration id, checks which ECUs answer, reads fault codes (read only), samples the standard PIDs while
you rev the engine, then probes a list of community-sourced mode 22 candidates. Everything sent and received goes to a
timestamped log file.

```bash
pip install pyserial
python3 tools/discover.py --device /dev/ttyUSB0 --sample-seconds 60     # Windows: --device COM3
```

`--unattended` keeps retrying until the adapter and car answer, then waits for the engine to start before sampling.
The mode 22 probe list is **unverified community data** from forum threads on other diesel models: treat every hit as
something to confirm with a real capture, never as a fact for your car. The log contains your **VIN**; redact it
before sharing.

### 2. Log a session with the Car Scanner app

Connect the Car Scanner phone app to the car with the same adapter, enable "save raw log" in its settings, add the
parameters you care about to a screen, and drive for 40 minutes or more (a DPF regeneration, if you catch one, is worth
a lot). Export two files from the same session: the raw ELM327 log (`log.txt`, headers on, echo on) and the decoded CSV.
Keep both offline: they contain your odometer and timestamps.

### 3. `carscanner_parse.py` + `carscanner_solve.py`: derive offsets and scaling

`carscanner_parse.py` splits the raw log into per-request reassembled ISO-TP payloads. It has a hard-coded `REQS` table
mapping each echoed request (including its frame-count digit) to a name; add your own car's requests there first.

```bash
python3 tools/carscanner_parse.py log.txt
pip install numpy
python3 tools/carscanner_solve.py log.txt export.csv
```

`carscanner_solve.py` aligns the log with the CSV using engine RPM, tries every byte offset and width (u8, i8, u16,
i16, u24, u32) of every request at a few sample shifts, keeps candidates with correlation above 0.995 and fits
`value = a*x + b`. Read the output critically: a strong correlation with an odd scale is often the wrong byte. Round
coefficients and check units (the ix35 reports metres; the logger shows miles).

Once a value is confirmed, add it to `src/obd/pid_registry.h` + `src/obd/pid_decode.cpp`, add a golden vector to
`test/test_decode/test_decode.cpp`, and document it in [docs/pids.md](../docs/pids.md). See
[CONTRIBUTING.md](../CONTRIBUTING.md).
