# Development

```bash
pio run -e cyd35        # build screen firmware
pio run -e freenove     # build headless firmware
pio test -e native      # host tests: ISO-TP reassembly + decoders against real captured replies
```
- Add a PID: one row in `src/obd/pid_registry.h` + a decoder in `pid_decode.cpp` + a golden vector in
  `test/test_decode/test_decode.cpp`. The CSV column appears automatically.
- Board differences live in `src/config.h` (pins) and `build_src_filter` in `platformio.ini`; display-only code is
  under `src/board`, `input`, `audio`, `ui` and guarded by `HAS_DISPLAY` in `main.cpp`.
- Discovery aids: `BLE_SCAN_ONLY`, `BLE_DUMP_SERVICES` in `config.h`. `SERIAL_RAW_ECHO 1` prints every exchange.
- Serial commands (115200): `d` dump logs, `p` tail current session, `e` erase logs, `O` oil-change reference
  reset; CYD only: `D`/`G` demo, `a r s l` alert sounds, `1 2` jingles.
- Telegram: set `ENABLE_TELEGRAM 1`, put token/chat id in `include/secrets.h`, and give it WiFi with internet.
- Never commit `include/secrets.h`.
