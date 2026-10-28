# Verified PIDs (2014 Hyundai ix35 1.7 CRDi, D4FD, CAN 11-bit 500k)

Every parameter was reverse-engineered from a Car Scanner session on this car: its raw ELM327 log aligned
sample-for-sample with its decoded CSV (`tools/carscanner_parse.py`, `carscanner_solve.py`). Requests need ATH1 and a
trailing frame-count digit.

| Header | Request | Gives |
|---|---|---|
| 7DF | 010C 010D 0104 0110 0105 010F 012C 013E 0142 | rpm, speed, load, MAF, coolant, IAT, EGR duty, catalyst temp, battery V |
| 7E0 | 211B | DPF differential pressure (u16@2 x0.5 hPa) |
| 7E0 | 2103 | EGT before DPF, distance since regen, odometer, regen active/burning flags |
| 7E0 | 21948001 | soot level (byte10 x100/255 g), intercooler temp |
| 7D4 | 2101 | steering ECU speed / angle / voltage |

## Per-column decode

How each CSV column is decoded. Request, header and frame digit come from `PID_TABLE` in
[`src/obd/pid_registry.h`](../src/obd/pid_registry.h); offsets and scales from
[`src/obd/pid_decode.cpp`](../src/obd/pid_decode.cpp) (checked by `pio test -e native`).

- **Request as sent** = request hex + the *frame-count digit*. ELM327 adapters accept one extra hex digit after a
  request meaning "expect this many response frames", so the adapter returns as soon as they arrive. `B` is hex 11.
  Some clone adapters answer `?`; the firmware then resends without the digit (`src/main.cpp`).
- **Header** is the CAN id set with `ATSH`. Replies come from `7E8` (for 7DF and 7E0) or `7DC` (for 7D4), see
  `rxIdFor()`.
- **Offset** counts from the ISO-TP payload start: byte `[0]` is the service echo (`0x41` for mode 01, `0x61` for
  mode 21). `u16 [2]` means bytes 2 and 3 big-endian. Example: reply `7E8 04 41 0C 1A F8` has payload `41 0C 1A F8`;
  `u16 [2]` = `0x1AF8` = 6904; x 0.25 = 1726 rpm.

| CSV column | Header | Request as sent | Decode from payload | Unit | Decoder |
|---|---|---|---|---|---|
| `engine_rpm` | 7DF | `010C1` | u16 [2] x 0.25 | rpm | `dec_rpm` |
| `vehicle_speed` | 7DF | `010D1` | u8 [2] | km/h | `dec_speed` |
| `engine_load_pct` | 7DF | `01041` | u8 [2] x 100/255 | % | `dec_load` |
| `maf_flow` | 7DF | `01101` | u16 [2] x 0.01 | g/s | `dec_maf` |
| `coolant_temp` | 7DF | `01051` | u8 [2] - 40 | C | `dec_coolant` |
| `control_module_v` (battery) | 7DF | `01421` | u16 [2] x 0.001 | V | `dec_module_voltage` |
| `egr_duty_pct` | 7DF | `012C1` | u8 [2] x 100/255 | % | `dec_egr` |
| `intake_air_temp_c` | 7DF | `010F1` | u8 [2] - 40 | C | `dec_iat` |
| `dpf_zone_temp_c` (catalyst temp, see below) | 7DF | `013E1` | u16 [2] x 0.1 - 40 | C | `dec_cat_temp` |
| `dpf_diff_pressure_hpa` | 7E0 | `211B1` | u16 [2] x 0.5 | hPa | `dec_dpf_diff_pressure` |
| `egt_before_dpf_c` | 7E0 | `2103B` | u16 [48] x 0.0183966 + 99.013 | C | `dec_egt_before_dpf` |
| `dpf_dist_since_regen_mi` | 7E0 | `2103B` | u24 [56] x 0.000621371 (metres to miles) | mi | `dec_dist_since_regen` |
| `odometer_mi` | 7E0 | `2103B` | u32 [59] x 0.000621371 (metres to miles) | mi | `dec_odometer` |
| `dpf_regen_active` | 7E0 | `2103B` | 1 if byte [71] == 4, else 0 | flag | `dec_regen_active` |
| `dpf_regen_burning` | 7E0 | `2103B` | 1 if byte [19] == 21, else 0 | flag | `dec_regen_burning` |
| `dpf_soot_level_g` | 7E0 | `219480012` | u8 [10] x 100/255 | g | `dec_soot` |
| `intercooler_temp_c` | 7E0 | `219480012` | u8 [6] - 50 | C | `dec_intercooler` |
| `eps_speed_kmh` | 7D4 | `21013` | u8 [5] | km/h | `dec_eps_speed` |
| `eps_steering_deg` | 7D4 | `21013` | i16 [11] x 0.1 (signed) | deg | `dec_eps_steering` |
| `eps_voltage_v` | 7D4 | `21013` | u8 [3] x 0.1 | V | `dec_eps_voltage` |
| `dpf_odo_at_last_regen_mi` | derived | none | `odometer_mi - dpf_dist_since_regen_mi` | mi | `src/main.cpp` |

Rows sharing a request (`21948001`, `2103`, `2101`) use one reply. The `2103` decoders need a payload of at least 75
bytes, which is why it is requested with 11 frames. If a payload is too short, or does not start with the expected
prefix (`41 <pid>` for mode 01, `61 1B`, `61 03`, `61 01`, or just `61` for `21948001`), the cell is left empty.
Soot is one byte scaled by 100/255, so it moves in steps of about 0.39 g.

- `dpf_zone_temp_c` is the standard "catalyst temperature, bank 1 sensor 2" PID `013E`. It kept its early name for
  continuity with existing logs, but it is **not** the DPF temperature; the DPF inlet temperature is
  `egt_before_dpf_c`. The web page labels it catalyst temperature.
- `dpf_regen_active` is 1 from the start of a regen (warm-up) until the distance-since-regen counter resets;
  `dpf_regen_burning` is 1 during the hot burn phase (about 500 C and above). Both flags are threshold tests on single
  bytes derived from one reference regen, not linear fits, and have not been validated over many events.
- `dpf_soot_level_g` is the ECU's own estimate; nothing in the logger measures the filter.
- The steering-ECU (`eps_*`) values are not DPF-relevant and are only in the CSV.
- Units are miles because the reference car is a UK, mile-dashboard car that reports metres.

Dead ends (do not retry): mode 22 does not exist on this ECU (`7F 22 11`); 010B (MAP), 0133 (baro), 012F (fuel
level), 0123 (rail pressure) are unsupported, so boost cannot be computed. "dpf_zone_temp_c" is really catalyst
temp 013E.

Unverified: the hot-idle pressure threshold (`DPF_IDLE_PRESSURE_THRESHOLD_HPA`) and the soot colour thresholds
(amber 14 g, red 17 g). Other cars: not supported yet; see [other-cars.md](other-cars.md).
Background: [ix35-known-issues.md](ix35-known-issues.md).
