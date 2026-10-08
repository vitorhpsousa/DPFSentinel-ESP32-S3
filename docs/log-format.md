# Log format

Files on the card, in `/obd`:
- `session_<N>.csv` — one row per second (`ROW_INTERVAL_MS`). N increments each boot (stored in flash).
- `raw_<N>.log` — tab-separated `millis, header, request, reply` for every adapter exchange, plus `TIME` markers.

CSV columns: `millis` (since boot), `unix_time` (UTC seconds, empty until the clock is known), then one column per
PID in [src/obd/pid_registry.h](../src/obd/pid_registry.h) order, then derived `dpf_odo_at_last_regen_mi`.
Empty cell = no valid reading in the last few seconds. The example below was logged before the clock was set, so `unix_time` is empty (see "Dating undated rows"). See [logs/examples/session_example.csv](../logs/examples/session_example.csv).

Dating undated rows: when the clock is set a line `<ms>  TIME  <source>  <unix>` is written to the raw log
(source `rtc`, `ntp` or `phone`). `python3 tools/date_session.py session_N.csv raw_N.log > dated.csv` fills `unix_time`
from it.

Storage behaviour: rows are flushed immediately; after 3 consecutive write failures the card is remounted;
if it stays unusable the logger falls back to internal flash (about 30 min). Nothing outside `/obd` is touched.
