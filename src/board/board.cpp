#include "board.h"
#include <Wire.h>
#include "driver/spi_master.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#define PIN_CS 10
#define PIN_CLK 12
#define PIN_D0 11
#define PIN_D1 13
#define PIN_D2 14
#define PIN_D3 9
#define PIN_TRST 48
#define PIN_TINT 47
#define TOUCH_ADDR 0x55
#define QSPI_HZ 40000000
#define TX_LEN 0x4000     // pixels per transaction

struct InitCmd { uint8_t cmd; const uint8_t *data; uint8_t len; uint16_t delay_ms; };
#define C(...) (const uint8_t[]){__VA_ARGS__}
// Table decoded straight from this board's own factory firmware (docs/cyd35-hardware-notes.md): the
// vendor image's own st77922_vendor_config_t / init command array, 56 entries up to RASET, plus the
// tail this panel's factory app actually runs (INVON, SLPOUT/120ms, DISPON, RAMWR, COLMOD 01, MADCTL 00, TEON).
// This is the panel's own bring-up sequence, extracted from a flash image of the exact unit in hand —
// not copied from any third-party example.
static const InitCmd initTable[] = {
  {0xF1, C(0x00), 1, 0},
  {0x60, C(0x00,0x00,0x00), 3, 0},
  {0x65, C(0x80), 1, 0},
  {0x79, C(0x06), 1, 0},
  {0x7B, C(0x00,0x08,0x08), 3, 0},
  {0x80, C(0x55,0x62,0x2F,0x17,0xF0,0x52,0x70,0xD2,0x52,0x62,0xEA), 11, 0},
  {0x81, C(0x26,0x52,0x72,0x27), 4, 0},
  {0x84, C(0x92,0x25), 2, 0},
  {0x87, C(0x10,0x10,0x58,0x00,0x02,0x3A), 6, 0},
  {0x88, C(0x00,0x00,0x2C,0x10,0x04,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x06), 15, 0},
  {0x89, C(0x00,0x00,0x00), 3, 0},
  {0x8A, C(0x13,0x00,0x2C,0x00,0x00,0x2C,0x10,0x10,0x00,0x3E,0x19), 11, 0},
  {0x8B, C(0x15,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x97,0x8E), 9, 0},
  {0x8C, C(0x1D,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x50,0x0F,0x01,0xC5,0x12,0x09), 13, 0},
  {0x8D, C(0x0C), 1, 0},
  {0x8E, C(0x33,0x01,0x0C,0x13,0x01,0x01), 6, 0},
  {0xB3, C(0x00,0x30), 2, 0},
  {0xF1, C(0x00), 1, 0},
  {0x71, C(0xD0), 1, 0},
  {0x66, C(0x02,0x3F), 2, 0},
  {0xBE, C(0x26,0x00,0x9D), 3, 0},
  {0x70, C(0x01,0xA6,0x11,0x40,0xE0,0x00,0x11,0x60,0x11,0x00,0x00,0x1A), 12, 0},
  {0x90, C(0x04,0x04,0x55,0x74,0x00,0x40,0x43,0x2D,0x2D), 9, 0},
  {0x91, C(0x04,0x04,0x55,0x75,0x00,0x40,0x42,0x2D,0x2D), 9, 0},
  {0x92, C(0x04,0x44,0x55,0xC0,0x06,0x00,0x07,0x05,0x90,0x2D), 10, 0},
  {0x93, C(0x04,0x43,0x11,0x00,0x00,0x00,0x00,0x05,0x90,0x2D), 10, 0},
  {0x94, C(0x00,0x00,0x00,0x00,0x00,0x00), 6, 0},
  {0x95, C(0x96,0x16,0x00,0x00,0xFF), 5, 0},
  {0x96, C(0x44,0x53,0x03,0x12,0x23,0x24,0x06,0x05,0x9A,0x2D,0x00,0x44), 12, 0},
  {0x97, C(0x44,0x53,0x47,0x56,0x20,0x20,0x02,0x01,0x9A,0x2D,0x00,0x44), 12, 0},
  {0xBA, C(0x55,0x9A,0x2D,0x9A,0x2D), 5, 0},
  {0x9A, C(0x40,0x00,0x06,0x00,0x00,0x00,0x00), 7, 0},
  {0x9B, C(0x00,0x00,0x06,0x00,0x00,0x00,0x00), 7, 0},
  {0x9C, C(0x5C,0x12,0x00,0x00,0x10,0x12,0x00,0x00,0x10,0x02,0x00,0x00,0x00), 13, 0},
  {0x9D, C(0x8A,0x51,0x00,0x00,0x00,0x80,0x1E,0x01), 8, 0},
  {0x9E, C(0x51,0x00,0x00,0x00,0x80,0x1E,0x01), 7, 0},
  {0xB4, C(0x1D,0x1C,0x1E,0x0B,0x14,0x02,0x13,0x09,0x1E,0x00,0x1E,0x10), 12, 0},
  {0xB5, C(0x1D,0x1C,0x1E,0x0A,0x15,0x03,0x11,0x08,0x1E,0x01,0x1E,0x12), 12, 0},
  {0xB6, C(0x77,0x77,0x00,0x0A,0xFF,0x0A,0xFF), 7, 0},
  {0x86, C(0xC6,0x04,0xB1,0x02,0x58,0x12,0x58,0x0C,0x13,0x01,0xA5,0x00,0xA5,0xA5), 14, 0},
  {0xB7, C(0x07,0x0A,0x0E,0x06,0x05,0x03,0x2B,0x03,0x03,0x42,0x07,0x10,0x10,0x2E,0x3F,0x0D), 16, 0},
  {0xB8, C(0x07,0x0A,0x0D,0x05,0x05,0x02,0x2B,0x02,0x03,0x42,0x06,0x10,0x0F,0x2E,0x3F,0x0D), 16, 0},
  {0xB9, C(0x23,0x23), 2, 0},
  {0xBF, C(0x10,0x14,0x14,0x0B,0x0B,0x0B), 6, 0},
  {0xF2, C(0x00), 1, 0},
  {0x73, C(0x04,0xDA,0x12,0x54,0x47), 5, 0},
  {0x77, C(0x6B,0x5B,0xFD,0xC3,0xC5), 5, 0},
  {0x7A, C(0x15,0x27), 2, 0},
  {0x7B, C(0x04,0x57), 2, 0},
  {0x7E, C(0x01,0x0E), 2, 0},
  {0xBF, C(0x36), 1, 0},
  {0xE3, C(0x40,0x40), 2, 0},
  {0xF0, C(0x00), 1, 0},
  {0xD0, C(0x00), 1, 0},
  {0x2A, C(0x00,0x00,0x01,0x3F), 4, 0},
  {0x2B, C(0x00,0x00,0x01,0xDF), 4, 0},
  // tail
  {0x21, nullptr, 0, 0},          // INVON (vendor tables do this; jlmeredith says one stack needs invert, other not: colours may look inverted)
  {0x11, nullptr, 0, 120},        // SLPOUT
  {0x29, nullptr, 0, 0},          // DISPON
  {0x2C, nullptr, 0, 0},          // RAMWR
  {0x3A, C(0x01), 1, 0},          // COLMOD (vendor writes 01, panel also takes 55; RGB565 assumed)
  {0x36, C(0x00), 1, 0},          // MADCTL portrait. Colour order (BGR per xiaozhi config) UNVERIFIED
  {0x35, C(0x01), 1, 20},         // TEON (TE pin 42 is NOT used/driven by this probe)
};


static spi_device_handle_t qspi;

static inline void csLow()  { GPIO.out_w1tc = (1u << PIN_CS); }
static inline void csHigh() { GPIO.out_w1ts = (1u << PIN_CS); }

static void lcdReg(uint8_t cmd, const uint8_t *data, uint8_t len) {
  csLow();
  spi_transaction_ext_t t; memset(&t, 0, sizeof(t));
  t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
  t.base.cmd = 0x02;
  t.base.addr = (uint32_t)cmd << 8;
  t.command_bits = 8;
  t.address_bits = 24;
  if (len) { t.base.tx_buffer = data; t.base.length = 8 * len; }
  spi_device_polling_transmit(qspi, (spi_transaction_t *)&t);
  csHigh();
}

static void lcdPushFrame(const uint16_t *px /* byte-swapped RGB565, LCD_W*LCD_H */) {
  const uint8_t xa[4] = {0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xFF};
  const uint8_t ya[4] = {0, 0, (LCD_H - 1) >> 8, (LCD_H - 1) & 0xFF};
  lcdReg(0x2A, xa, 4);
  lcdReg(0x2B, ya, 4);
  size_t total = (size_t)LCD_W * LCD_H;
  bool first = true;
  spi_transaction_ext_t t; memset(&t, 0, sizeof(t));
  csLow();
  while (total) {
    if (first) {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
      t.base.cmd = 0x32; t.base.addr = 0x3C << 8;
      t.command_bits = 8; t.address_bits = 24;
      first = false;
    } else {
      // continuation chunks: no cmd/addr/dummy phases (same as vendor example)
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
      t.command_bits = 0; t.address_bits = 0; t.dummy_bits = 0;
    }
    size_t n = total > TX_LEN ? TX_LEN : total;
    t.base.tx_buffer = px;
    t.base.length = n * 16;
    spi_device_polling_transmit(qspi, (spi_transaction_t *)&t);
    px += n; total -= n;
  }
  csHigh();
}

static GFXcanvas16 *s_canvas = nullptr;
static uint16_t *s_txbuf = nullptr;
static bool s_lcdUp = false;

GFXcanvas16 *boardCanvas() { return s_canvas; }
void boardSetBacklight(uint8_t v) { analogWrite(BOARD_PIN_BL, v); }

void boardPresent() {
    if (!s_canvas || !s_txbuf || !s_lcdUp) return;
    const uint16_t *src = s_canvas->getBuffer();
    // Rotate the UI_W x UI_H canvas into the LCD_W x LCD_H tx buffer (byte-swapped), in blocks of
    // 16 source pixels so the 16 destination rows being written stay in cache.
    const int B = 16;
#if BOARD_ROTATION == 3
    // canvas (cx,cy) -> panel (cy, LCD_H-1-cx)
    for (int cx0 = 0; cx0 < UI_W; cx0 += B)
        for (int cy = 0; cy < UI_H; cy++) {
            const uint16_t *s = src + (size_t)cy * UI_W + cx0;
            uint16_t *d = s_txbuf + (size_t)(LCD_H - 1 - cx0) * LCD_W + cy;
            for (int k = 0; k < B; k++, d -= LCD_W) *d = __builtin_bswap16(s[k]);
        }
#else
    // canvas (cx,cy) -> panel (LCD_W-1-cy, cx)
    for (int cx0 = 0; cx0 < UI_W; cx0 += B)
        for (int cy = 0; cy < UI_H; cy++) {
            const uint16_t *s = src + (size_t)cy * UI_W + cx0;
            uint16_t *d = s_txbuf + (size_t)cx0 * LCD_W + (LCD_W - 1 - cy);
            for (int k = 0; k < B; k++, d += LCD_W) *d = __builtin_bswap16(s[k]);
        }
#endif
    lcdPushFrame(s_txbuf);
}

static void lcdInit() {
    pinMode(PIN_CS, OUTPUT); digitalWrite(PIN_CS, HIGH);
    spi_bus_config_t bus = {};
    bus.data0_io_num = PIN_D0; bus.data1_io_num = PIN_D1; bus.sclk_io_num = PIN_CLK;
    bus.data2_io_num = PIN_D2; bus.data3_io_num = PIN_D3;
    bus.max_transfer_sz = TX_LEN * 2 + 8;
    bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {};
    dev.mode = 0; dev.clock_speed_hz = QSPI_HZ; dev.spics_io_num = -1;   // CS toggled by hand
    dev.flags = SPI_DEVICE_HALFDUPLEX; dev.queue_size = 4;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &qspi));
    for (size_t i = 0; i < sizeof(initTable) / sizeof(initTable[0]); i++) {
        lcdReg(initTable[i].cmd, initTable[i].data, initTable[i].len);
        if (initTable[i].delay_ms) delay(initTable[i].delay_ms);
    }
    s_lcdUp = true;
}

bool boardInit() {
    pinMode(PIN_TINT, INPUT);
    pinMode(BOARD_PIN_BL, OUTPUT); digitalWrite(BOARD_PIN_BL, LOW);   // on after the first frame
    Wire.begin(BOARD_PIN_SDA, BOARD_PIN_SCL, 400000);
    s_canvas = new GFXcanvas16(UI_W, UI_H);
    s_txbuf = (uint16_t *)heap_caps_malloc((size_t)LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_canvas || !s_canvas->getBuffer() || !s_txbuf) return false;
    lcdInit();
    // The touch controller is part of the display driver: reset it AFTER the LCD init.
    pinMode(PIN_TRST, OUTPUT); digitalWrite(PIN_TRST, LOW); delay(10);
    digitalWrite(PIN_TRST, HIGH); delay(100);
    s_canvas->fillScreen(0);
    boardPresent();
    boardSetBacklight(255);
    return true;
}

// One burst read from register 0x0010 (16-bit big-endian address): 4 header bytes + 5 points x 7 bytes.
// The burst read clears INT; separate reads only ever returned the first touch.
bool boardTouchRead(int16_t &x, int16_t &y, bool &pressed) {
    uint8_t d[4 + 5 * 7];
    Wire.beginTransmission(TOUCH_ADDR); Wire.write((uint8_t)0x00); Wire.write((uint8_t)0x10);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)sizeof d) != sizeof d) return false;
    for (auto &v : d) v = Wire.read();
    pressed = false;
    for (int i = 0; i < 5; i++) {
        const uint8_t *p = d + 4 + i * 7;
        if (!(p[0] & 0x80)) continue;
        x = ((p[0] & 0x3F) << 8) | p[1];
        y = ((p[2] & 0x3F) << 8) | p[3];
        if (x < LCD_W && y < LCD_H) {
            pressed = true;
            int16_t px = x, py = y;   // raw panel coords -> landscape canvas coords
#if BOARD_ROTATION == 3
            x = LCD_H - 1 - py; y = px;
#else
            x = py; y = LCD_W - 1 - px;
#endif
        }
        break;
    }
    return true;
}
