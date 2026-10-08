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

Dead ends (do not retry): mode 22 does not exist on this ECU (`7F 22 11`); 010B (MAP), 0133 (baro), 012F (fuel
level), 0123 (rail pressure) are unsupported, so boost cannot be computed. "dpf_zone_temp_c" is really catalyst
temp 013E.

Unverified: the hot-idle pressure threshold (`DPF_IDLE_PRESSURE_THRESHOLD_HPA`) and the soot colour thresholds
(amber 14 g, red 17 g). Other cars: not supported yet.
Background: [ix35-known-issues.md](ix35-known-issues.md).
