# Adapting the logger to another car

> **Status:** the logger has been verified on one car (2014 Hyundai ix35 1.7 CRDi, D4FD). Nothing on this page has been done on any other car. The method below is what the repository's own tooling supports today; steps that need tooling that does not exist yet are marked **(proposed)**. Read [disclaimer.md](disclaimer.md) first.

## Ground rules

1. **Read-only.** Only ask for readings. Never send mode 04 (clear codes), writes, session changes (`10 xx`), security access (`27`), routine control (`31`) or anything that reprograms an ECU. The discovery script sends only services 01, 03, 06, 07, 09, 0A, 22 and adapter (`AT`/`ST`) commands.
2. **Nothing becomes a logged column until it is confirmed** against a real capture from the same car. PID lists from forums and chatbots have been wrong (a mode 22 list did not exist on the ix35).
3. **Engine off, parked, adapter powered from the port.** Do the discovery with the ignition on, engine running only where the script asks, in a safe place. Do not fiddle with a laptop while driving.
4. **Privacy.** Discovery output includes the VIN, and logs contain the odometer. Redact before sharing.
5. Not every car has a DPF, and not every DPF car exposes soot or pressure through OBD. You may end up with a plain logger.

## Which parts are generic, which are car-specific

| Generic (reusable) | Car-specific (this is what you replace) |
|---|---|
| BLE ELM327 client (`src/obd/elm_client.cpp`), ISO-TP reassembly (`src/obd/isotp.cpp`), session storage (CSV plus raw log, `src/logging/`), web page plumbing, Telegram transport | PID table and decoders (`src/obd/pid_registry.h` + `src/obd/pid_decode.cpp`), protocol set with `ATSP6` (`src/obd/elm_client.cpp`), reply ids (`rxIdFor()` in `pid_registry.h`), derived columns (`src/main.cpp`), regen-flag logic, thresholds, next-regen estimate (`src/dpf/health.cpp`), web tile and screen labels |

## The method

### Step 0: What you need

- The same OBD adapter type you will log with (ELM327 clone, OBDLink MX+, BLE). Some clones do not support the frame-count digit or headers reliably; note which.
- A read-only discovery tool that can log raw adapter traffic. The firmware logs already-known PIDs; it does not probe for unknown ones. Use [`tools/discover.py`](../tools/discover.py) instead: it runs on any computer with an ELM327-type adapter that shows up as a serial port (USB, or classic Bluetooth paired to the computer; a BLE-only adapter will not work) and does the discovery workflow below. See [`tools/README.md`](../tools/README.md). **(proposed)**: a discovery mode built into the firmware, so no separate computer is needed.
- Optional but strongly recommended: a phone app that can show the values you want *and* save a raw log (Car Scanner can export raw ELM327 traffic plus a decoded CSV). This is your ground truth.

### Step 1: Identify the car and protocol

Run the discovery tool against the adapter:

```bash
# ignition on, engine off, adapter plugged in (USB) or paired (classic Bluetooth)
pip install pyserial
python3 tools/discover.py --device <your-serial-port> --sample-seconds 60
```

It runs these stages and writes every request and reply to a log file (`--out` picks the name):

1. Adapter and protocol: `ATDPN` / `ATDP` (which OBD protocol the car answered; the ix35 uses ISO 15765-4 CAN 11-bit 500 kbaud, `ATSP6`).
2. Supported PID bitmaps for mode 01 (`0100`, `0120`, `0140`, ...).
3. Identity (mode 09: VIN, calibration ID).
4. Module census: `0100` on `7E0` to `7E7`, to see which ECUs answer on the OBD port.
5. Fault codes (read only, raw).
6. Mode 06 raw for a few monitors.
7. One pass of every supported mode 01 PID.
8. A REV window (hold 2000 to 3000 rpm) so values move.
9. DPF / enhanced candidate probes on mode 22 (`7F 22 11` means the service is not supported).

`--unattended` waits for the engine to start (`--wait-minutes`). Send the log back to whoever is helping you, after removing the VIN if you prefer.

**Read the output for these answers:**

| Question | Where to look | Consequence |
|---|---|---|
| Is it CAN 11-bit 500k? | `ATDPN` says `A6` or `6` | The logger's ISO-TP code (`src/obd/isotp.cpp`) only handles 11-bit ids. 29-bit or K-line needs code changes. |
| Which ECU carries engine data? | Module census: usually `7E0` (reply `7E8`) | Set the request header (`ATSH`) and reply id accordingly. On the ix35, mode 01 uses `7DF` (broadcast) and reply id `7E8`. |
| What standard PIDs exist? | Bitmap stage | These are safe to log with the standard formulas (RPM, coolant, MAF...). |
| Is there any manufacturer-specific data? | Mode 21 or 22 probes | Mode 22 replies that are not `7F 22 11` are candidates. |

### Step 2: Decide what you want to log

Start with the **standard PIDs** the car declares: they need no reverse engineering (formulas in SAE J1979). Add manufacturer PIDs only for what you actually care about, for example soot mass, DPF differential pressure, temperatures before/after the filter, regen state, distance since last regen.

There are two sources for manufacturer PIDs, in order of trust:

1. **Your own capture from a phone app that already shows the value** (see Step 3). Verified.
2. **Community PID lists** (forums, Torque PID files, OBD Fusion PID packs). These are **hints to probe, never facts.** They are often for a different model year or engine. [`tools/discover.py`](../tools/discover.py) probes some community-sourced candidates from other vehicles' forum threads and labels them UNVERIFIED in its source.

### Step 3: Capture a reference

1. Connect the phone app to the car with the same adapter. In Car Scanner, enable the option to save raw logs, add the parameters you want on the screen, and drive for 40 minutes or more (a regen event, if possible, is worth a lot: the soot and pressure values change most then).
2. Export the raw ELM327 log (`log.txt`, headers on, echo on) and the decoded CSV **from the same session**.
3. Keep both offline. They contain your odometer and timestamps.

Why the raw log matters: it contains the exact request bytes (including the frame-count digit and headers) and the exact reply bytes, which you can decode yourself.

### Step 4: Decode

A two-step decode method (split the raw log into requests/reassemble the ISO-TP payloads, then fit `value = a*x + b` against a reference CSV by byte offset and width) is described in [CONTRIBUTING.md](../CONTRIBUTING.md) and implemented in [`tools/carscanner_parse.py`](../tools/carscanner_parse.py) and [`tools/carscanner_solve.py`](../tools/carscanner_solve.py) (see [`tools/README.md`](../tools/README.md)). It is how the ix35's own decoders were originally derived.

1. Split the raw log into requests and reassemble the ISO-TP payloads per request. The ix35 decode used a hard-coded request table and accepted reply ids `7E8` and `7DC` only — any equivalent tool needs its own table for your car.
2. Align the raw-log series with a reference CSV (for example by RPM, matching a `010C` request in the log to an "Engine RPM" column), then try every byte offset and width of every request and fit `value = a*x + b`, keeping candidates with a high correlation (0.995 was used for the ix35). Columns with too few samples or no variance should be skipped.
3. Read candidates critically: a strong correlation with an odd scale is often the wrong byte. Round coefficients, check units.

Limits: this only finds linear numeric fits. Flags (regen active) and enumerations need manual work, comparing raw bytes before and during the event. The ix35's two regen flags came from one reference regen and are marked as not validated.

**(proposed)** A generic replay tool that takes a raw log and prints decoded values, so you can validate without a CSV. Not written.

For a **quick look at raw replies** without a reference: every raw adapter exchange is already written to `raw_N.log` (and printed on serial with `SERIAL_RAW_ECHO 1` in `src/config.h`). Add a temporary probe row, match its bytes by timestamp against a phone screen, then remove the probe once decoded.

### Step 5: Add the decoders

For each confirmed value:

- Add a decoder to `src/obd/pid_decode.cpp` and a `PID_TABLE` row to `src/obd/pid_registry.h`.
- If replies come from a new id, extend `rxIdFor()` in `src/obd/pid_registry.h`.
- Change `ATSP6` in `src/obd/elm_client.cpp` if your car uses another protocol.
- Add a golden vector from your raw log to `test/test_decode/test_decode.cpp` and run `pio test -e native`.

Column names are effectively an API (the web page, the screen and Telegram text look columns up by name).

### Step 6: Validate against the dash and the app

Do not trust a decode because the number looks plausible.

1. **Logged run**: drive with the new logger and the phone app in parallel if the adapter allows two connections (many BLE adapters do not: one central at a time). If it does not, log one drive with the logger and compare against a second run of the app under similar conditions.
2. **Compare in numbers**: for the same moment, does the CSV value match the app to within display resolution? Check idle, warm-up, cruise, engine off (values should be blank or frozen).
3. **Check against the dash and a physical fact**: coolant against the dash gauge, speed against the speedometer (the speedo is usually a little optimistic), odometer against the dash (mind miles and km), battery against a multimeter. For DPF values there is no dash reading; use the app and the car's regen behaviour (fan, idle raise, dash symbol, fuel use).
4. **Check limits**: is the value stable and sensible over a whole drive, no overflow or sign errors (temperatures below zero, big numbers after 32768)?
5. **Check the raw log** for the request being answered every time (blank rows suggest the wrong header, wrong protocol or the frame-count digit rejected).
6. **Record how many samples and how big the error** in your PID doc for that car.

### Step 7: Set thresholds for your car

The ix35's soot colour bands (14 g / 17 g / 28 g) and 20 hPa idle flag are the owner's choices for one car and one unverified idle test. They do not carry over. Log first, then pick your own thresholds from your own history, and keep them clearly labelled as personal choices.

### Step 8: Publish what you learned

Add a per-car page next to [pids.md](pids.md) with: engine and year, adapter, protocol, request, header, frame digit, offset, scale, unit, number of samples and error, and what does NOT exist on that car (unsupported PIDs are as useful as supported ones). Redact VIN and plates.

## Common problems

| Symptom | Likely cause |
|---|---|
| Endless empty rows | Wrong protocol (`ATSP6` hard-coded), wrong header, adapter rejects the frame-count digit (the firmware resends without it on `?`) |
| Only one ECU answers, values missing | On the `7DF` broadcast several ECUs reply and the reply-id filter drops the one you want; try a physical header such as `ATSH7E0` |
| 29-bit ids | Not supported by `src/obd/isotp.cpp` |
| Large replies cut off | 96-byte payload buffer (`src/main.cpp`) |
| Two apps cannot connect at once | BLE adapters usually allow one central at a time |
| Mode 22 returns `7F 22 11` | Not supported on that ECU; try mode 21 or another service |

## Not covered

Pre-2008 cars (K-line/ISO 9141/KWP2000, J1850), heavy vehicles (J1939), and EVs. Some manufacturers gate diagnostic data behind a gateway module that an OBD adapter cannot pass; nothing here works around that, and nothing should.
