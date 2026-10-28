# Contributing

This is a hobby project verified on one car. The most valuable contributions are verified PIDs for other engines and
fixes to the known issues listed at the end. The guiding rule:

> **Nothing becomes a logged column until it has been confirmed against a real capture from the car.** PID lists from
> chatbots and forums have been wrong before: a chatbot-supplied mode 22 list was useless because the ix35 has no
> mode 22.

## Read-only, always

The logger only asks for readings. Do not add requests that clear codes (mode 04), write, change diagnostic session or
reprogram anything. Any discovery tooling for another car must likewise be read-only. Anything that could change ECU
state does not belong in this repository.

## Dev setup

Firmware: PlatformIO, from the repo root ([docs/development.md](docs/development.md)):

```bash
pio run -e cyd35        # screen board (also cyd35_flipped, cyd35_landscape)
pio run -e freenove     # headless board
pio test -e native      # host unit tests: ISO-TP reassembly + decoders against real captured replies
```

Copy `include/secrets.example.h` to `include/secrets.h` for your own values; it is git-ignored. Never commit real
credentials.

The Python helpers in [tools/](tools/README.md) are standalone. `discover.py` needs `pyserial`, `carscanner_solve.py`
needs `numpy`:

```bash
python3 -m venv venv
venv/bin/pip install pyserial numpy
```

## Tests

`pio test -e native` builds `src/obd/pid_decode.cpp` and `src/obd/isotp.cpp` on your computer and runs
`test/test_decode/test_decode.cpp`, which checks ISO-TP reassembly and every decoder against replies captured from the
car. Run it before sending a change to the decoders, and add a golden vector for any new PID. Also make sure
`pio run -e cyd35` and `pio run -e freenove` still build.

## Adding a PID (the Car Scanner method)

1. **Capture a reference.** Run Car Scanner (or another app that can export its raw traffic) with the same adapter on a
   real drive of 40 minutes or more. Export the raw ELM327 log (`log.txt`, headers on, echo on) and the decoded CSV.
   They must come from the same session. Do not put these exports, or your own logs, in the repository without
   checking them (see Privacy).
2. **Teach the parser your requests.** [`tools/carscanner_parse.py`](tools/carscanner_parse.py) has a hard-coded
   `REQS` table mapping each echoed request *including its frame-count digit* (for example `219480012`, `2103B`) to a
   name, and it only accepts reply ids `7E8` and `7DC`. Add your new request there. `carscanner_solve.py` also needs
   the request `010C` (RPM) in the log to align the two files, and a CSV column literally named `Engine RPM (rpm)`.
3. **Solve.** `python3 tools/carscanner_solve.py log.txt export.csv`. For each CSV column it tries every byte offset
   and width (u8, i8, u16, i16, u24, u32) of every request at sample shifts of -2 to +2, keeps candidates with
   correlation above 0.995 and fits `value = a*x + b`. Columns with fewer than 1000 samples or no variance are skipped.
4. **Read the result critically.** Round coefficients to sensible values, check units (the car reports metres, the
   logger shows miles), and look at the residual error and sample count printed for each candidate. A high correlation
   with a strange scale is often the wrong byte.
5. **Add the decoder:**
   - a decoder in `src/obd/pid_decode.cpp` and a `PID_TABLE` row in `src/obd/pid_registry.h`, including the poll
     period;
   - `rxIdFor()` in `src/obd/pid_registry.h` if the ECU replies from a new id;
   - a golden vector (a real reply from your raw log and the expected value) in `test/test_decode/test_decode.cpp`.
6. **Prove it in your own logger.** Log a drive, compare your CSV against a second Car Scanner run, and check the raw
   log for the request. Column names are effectively an API (the web page, the screen and Telegram text look them up
   by name), so rename with care. Files are self-describing because the header row lists every column, but older files
   will not have your new column.
7. **Document it** in [docs/pids.md](docs/pids.md): request, header, frame digit, offset, scale, how it was validated
   and how many samples.

For a different car, start with read-only discovery on that car rather than a PID list from the internet:
[`tools/discover.py`](tools/discover.py) ([tools/README.md](tools/README.md)) and
[docs/other-cars.md](docs/other-cars.md).

## Privacy

Raw logs and CSVs contain your odometer reading and timestamps. Discovery output (mode 09) contains the **VIN**, and a
photo or filename may contain the registration plate. Redact these before sharing a log in an issue. Never commit
adapter Bluetooth addresses, Wi-Fi passwords, or Telegram tokens and chat ids.

## Sign off your commits

By contributing you certify the [Developer Certificate of Origin](DCO): in short, that you wrote the contribution
yourself or otherwise have the right to submit it under this project's licence (GPL-3.0-or-later). Add your
certification to every commit with `git commit -s`, which appends a line like:

```
Signed-off-by: Random J Developer <random@developer.example.org>
```

## Known issues you could fix

- The idle-blockage flag ignores engine temperature, so it is not really a "hot idle" test.
- The time zone is a build-time setting (`TZ_RULE` in `src/config.h`, UK by default).
- More in [docs/ix35-known-issues.md](docs/ix35-known-issues.md).
