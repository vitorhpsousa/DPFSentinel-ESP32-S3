# Dash mileage doesn't match the logger

On several cars the dash odometer and the ECU's own odometer (`odometer_mi` in the CSV, and what the
screen shows) are simply two different counters. Many manufacturers derive the dash figure from the
ABS/wheel-speed sensors — which depend on tyre size and can drift from the factory spec over the life of
a set of tyres — while the ECU counts from its own, separate source. Neither one is "wrong"; a gap of a
few miles up to a few hundred over the car's lifetime is normal and isn't a fault with this logger, the
car, or the OBD adapter.

This project never changes what's logged to the CSV — `odometer_mi` there is always the ECU's raw value,
so your data stays comparable across sessions and to anyone else's logs. The offset below only affects
what's shown to you: the CYD board's screen, and the `odometer_offset_mi` field in `/api/live`.

## Setting it

1. With the engine running and the adapter connected (so the logger has a live ECU reading), find the
   board's IP — it's printed to serial at boot, and shown on the CYD screen's system page.
2. On your phone or any device on the same network, open:
   ```
   http://<board-ip>/api/odometer?dash=<your dash's current mileage>
   ```
   e.g. `http://10.1.1.29/api/odometer?dash=125624`.
3. The board replies with the offset it worked out and stored, e.g.
   `offset set: dash 125624 - ECU 125271 = +353.0 mi (applied to the screen from now on)`.

That's it — no reflashing, no rebuild. The offset is saved to flash (NVS) and survives a reboot or power
cut. It's a single constant, so it only needs setting once unless you fit a different-size wheel/tyre on
this car (which would change how fast the dash drifts from the ECU) or move this board to another car.

If `/api/odometer` replies "no live odometer reading yet", the car isn't connected over OBD at that
moment — try again with the engine on.

## Checking it

`/api/live` includes `"odometer_offset_mi"` at the top level — the number currently being added to the
screen's reading. `0.0` means it's never been set (or was set to a calculated offset of exactly zero).
