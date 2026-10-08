# Hardware

## Boards
**CYD 3.5" (env `cyd35`)** — ESP32-S3, 16 MB flash, 8 MB PSRAM, ST77922 QSPI 320x480, Sitronix touch, ES8311 speaker,
microSD on SD_MMC 4-bit (CLK5 CMD4 D0-D3 6/7/2/3). All pins were proven from the factory firmware and live probing:
[cyd35-hardware-notes.md](cyd35-hardware-notes.md), [cyd35-board-notes.md](cyd35-board-notes.md).
Back up the factory flash before first flashing (`esptool read_flash 0 0x1000000 backup.bin`).

**Freenove ESP32-S3 WROOM (env `freenove`)** — no screen; the camera is unused. microSD on SD_MMC **1-bit**:
CLK 39, CMD 38, D0 40 (D1-D3 unused). These pins come from Freenove's published board layout and are **not yet
verified on this unit**: first boot should print `Logging to SD`; if it says flash, check the pins in
`src/config.h`. GPIO 1 and 2 are free for status LEDs (`LED_REGEN_PIN`, `LED_TEMP_WARN_PIN`).

## OBD adapter
A BLE ELM327 clone. The firmware scans for names containing OBD/ELM/VLINK/VEEPEAK/IOS-, auto-detects the UART
characteristics and prints them. Pin them in `src/config.h` (`BLE_TARGET_ADDRESS`) once known. If the adapter turns
out to be Bluetooth Classic only, an ESP32-S3 cannot use it (BLE only) — use the Pi build later.
Only one BLE central can connect at a time: close the phone app first.

## Power
Use a switched 5 V USB source that stays up during cranking, or a power bank that doesn't auto-off on low draw.
The logger flushes the CSV every row, so a sudden power cut loses at most one row.
