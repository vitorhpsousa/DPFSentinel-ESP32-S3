#pragma once
// 3.5" 320x480 ST77922 QSPI ESP32-S3 board (CYD-style). Pins proven from the factory firmware,
// see docs/cyd35-hardware-notes.md. The display has no library: raw IDF spi_master QSPI driver +
// an Adafruit GFXcanvas16 (PSRAM) that is pushed to the panel by boardPresent().
#include <Arduino.h>
#include <Adafruit_GFX.h>

#define LCD_W 320            // physical panel (portrait) size
#define LCD_H 480
// The UI canvas is LANDSCAPE. boardPresent() rotates it into the panel buffer, boardTouchRead()
// maps raw panel touches back into canvas coordinates. 1 = 90 deg clockwise, 3 = counter-clockwise.
#ifndef BOARD_ROTATION
#define BOARD_ROTATION 1
#endif
#define UI_W 480
#define UI_H 320
#define BOARD_PIN_BL 41      // backlight, active high, LEDC PWM
#define BOARD_PIN_SDA 38
#define BOARD_PIN_SCL 39

// Starts I2C (400 kHz), the panel, the touch controller (reset after the LCD init) and the backlight.
// Returns false if the canvas/buffers could not be allocated.
bool boardInit();
GFXcanvas16 *boardCanvas();      // UI_W x UI_H landscape; draw here, then boardPresent()
void boardPresent();             // push the whole canvas to the panel (~20 ms; call from ONE task only)
void boardSetBacklight(uint8_t v);   // 0..255
// Reads the touch controller with one 39-byte burst. Returns false on I2C error.
// pressed=false means no finger; x 0..UI_W-1, y 0..UI_H-1 (landscape canvas coordinates) otherwise.
bool boardTouchRead(int16_t &x, int16_t &y, bool &pressed);
