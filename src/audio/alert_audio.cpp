#include "alert_audio.h"
#include <Wire.h>
#include <math.h>
#include "driver/i2s.h"   // legacy driver: the logger stays on Arduino-ESP32 2.x because NimBLE-Arduino 1.4.3 crashes on 3.x
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const uint32_t SR = 16000;  // PROVEN: factory firmware uses 16 kHz/16-bit; MCLK = 256*SR = 4.096 MHz
static QueueHandle_t s_q = nullptr;
static volatile bool s_ready = false;
static volatile bool s_muted = false;

struct Tone { uint16_t hz; uint16_t ms; uint8_t vol; };  // hz==0 => silence

static bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(AUDIO_ES8311_ADDR);
  Wire.write(reg); Wire.write(v);
  return Wire.endTransmission() == 0;
}
static bool rd(uint8_t reg, uint8_t &v) {
  Wire.beginTransmission(AUDIO_ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AUDIO_ES8311_ADDR, (uint8_t)1) != 1) return false;
  v = Wire.read(); return true;
}

static void paSet(bool on) { digitalWrite(AUDIO_PIN_PA, (on == (AUDIO_PA_ACTIVE_HIGH != 0)) ? HIGH : LOW); }

// ES8311 register values below are this chip's own required configuration (datasheet-level facts: power-up
// sequence, clock dividers for 16 kHz/16-bit, DAC enable) rather than any copied driver code; cross-checked
// against Espressif's own public es8311 reference driver. 16-bit I2S slave, MCLK from pin.
static bool codecInit() {
  uint8_t id; if (!rd(0xFD, id)) return false;  // chip id reg (ES8311 CHD1 = 0x83); ACK is the real check
  bool ok = true;
  ok &= wr(0x00, 0x1F); delay(20);
  ok &= wr(0x00, 0x00);
  ok &= wr(0x00, 0x80);            // power on, slave mode
  ok &= wr(0x01, 0x3F);            // all clocks on, MCLK from pin
  // Coefficients for MCLK 4.096 MHz / 16 kHz (table row: pre_div 1, mult 0, adc/dac_div 1, fs 0, lrck 0x00FF, bclk 4, osr 0x10)
  uint8_t r;
  if (!rd(0x02, r)) return false;
  r &= 0x07; r |= (1 - 1) << 5; r |= 0 << 3; ok &= wr(0x02, r);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x04, 0x10);
  ok &= wr(0x05, 0x00);
  if (!rd(0x06, r)) return false;
  r &= 0xE0; r &= ~(1 << 5); r |= (4 - 1); ok &= wr(0x06, r);  // BCLK div 4, SCLK not inverted
  if (!rd(0x07, r)) return false;
  r &= 0xC0; r |= 0x00; ok &= wr(0x07, r);
  ok &= wr(0x08, 0xFF);
  ok &= wr(0x09, 0x0C);            // SDP in: 16-bit I2S
  ok &= wr(0x0A, 0x0C);            // SDP out: 16-bit
  ok &= wr(0x0D, 0x01);            // power up analog
  ok &= wr(0x0E, 0x02);
  ok &= wr(0x12, 0x00);            // power up DAC
  ok &= wr(0x13, 0x10);            // enable HP drive output
  ok &= wr(0x1C, 0x6A);
  ok &= wr(0x37, 0x08);            // bypass DAC EQ
  // DAC volume reg 0x32: codec-side ceiling. GUESS: 0xBF (~75%) leaves headroom; software scaling does the real capping.
  ok &= wr(0x32, 0xBF);
  if (rd(0x31, r)) ok &= wr(0x31, r & ~((1 << 6) | (1 << 5)));  // unmute
  return ok;
}

static void playTone(const Tone &t) {
  const int chunk = 256;  // frames per write
  static int16_t buf[256 * 2];
  uint32_t total = (uint32_t)SR * t.ms / 1000;
  uint8_t vol = t.vol > AUDIO_MAX_VOLUME_PCT ? AUDIO_MAX_VOLUME_PCT : t.vol;
  float amp = 32767.0f * 0.9f * vol / 100.0f;
  const uint32_t ramp = SR / 200;  // 5 ms fade in/out to avoid clicks
  float phase = 0, inc = t.hz ? 2.0f * (float)M_PI * t.hz / SR : 0;
  for (uint32_t n = 0; n < total;) {
    int cnt = (total - n) < (uint32_t)chunk ? (int)(total - n) : chunk;
    for (int i = 0; i < cnt; i++, n++) {
      float env = 1.0f;
      if (n < ramp) env = (float)n / ramp;
      else if (total - n < ramp) env = (float)(total - n) / ramp;
#if AUDIO_SQUARE
      // 8-bit arcade voice: square wave (harsher, so scaled down to keep the loudness comparable to the sine)
      int16_t s = t.hz ? (int16_t)((sinf(phase) >= 0 ? 0.55f : -0.55f) * amp * env) : 0;
#else
      int16_t s = t.hz ? (int16_t)(sinf(phase) * amp * env) : 0;
#endif
      phase += inc; if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
      buf[2 * i] = s; buf[2 * i + 1] = s;
    }
    size_t w = 0;
    i2s_write(I2S_NUM_0, buf, cnt * 4, &w, 1000);
  }
}

static void audioTask(void *) {
  Tone t; bool paOn = false;
  for (;;) {
    if (xQueueReceive(s_q, &t, paOn ? pdMS_TO_TICKS(300) : portMAX_DELAY) == pdTRUE) {
      if (!paOn) { paSet(true); paOn = true; delay(30); }  // let PA settle; GUESS 30 ms
      playTone(t);
    } else if (paOn) {  // idle 300 ms: flush silence, PA off to avoid hiss
      static int16_t z[128 * 2] = {0}; size_t w;
      i2s_write(I2S_NUM_0, z, sizeof z, &w, 100);
      paSet(false); paOn = false;
    }
  }
}

bool audioBegin() {
  if (s_ready) return true;
  pinMode(AUDIO_PIN_PA, OUTPUT); paSet(false);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SR;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  // ESP32-S3 has no APLL, so fixed_mclk is not used (it only applies with use_apll). MCLK = SR * mclk_multiple = 4.096 MHz;
  // libdriver's i2s_check_set_mclk() routes it to pins.mck_io_num through the GPIO matrix (any GPIO on S3).
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = AUDIO_PIN_MCLK;
  pins.bck_io_num = AUDIO_PIN_BCLK;
  pins.ws_io_num = AUDIO_PIN_WS;
  pins.data_out_num = AUDIO_PIN_DOUT;
  pins.data_in_num = AUDIO_PIN_DIN;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  delay(10);
  if (!codecInit()) return false;
  s_q = xQueueCreate(16, sizeof(Tone));
  if (!s_q) return false;
  s_ready = true;
  xTaskCreate(audioTask, "audio", 6144, nullptr, 1, nullptr);  // prio 1 = same as loop(); it only blocks in i2s_write/queue, never the logger
  return true;
}

static void enqueue(uint16_t hz, uint16_t ms, uint8_t v) {
  if (!s_ready) return;
  Tone t{hz, ms, v};
  xQueueSend(s_q, &t, 0);  // never blocks; drops if queue full
}

void audioBeep(uint16_t hz, uint16_t ms, uint8_t volumePct) {
  if (s_muted) return;
  enqueue(hz, ms, volumePct);
}

// Short, distinct patterns kept in 0.6-1.2 kHz (2.5 kHz is much quieter on this speaker). Tune by ear.
void audioAlert(AlertKind kind) {
  if (s_muted) return;
  const uint8_t v = AUDIO_MAX_VOLUME_PCT;
  switch (kind) {
    // Original arcade-style alerts (own melodies), square-wave voice.
    case REGEN_START: enqueue(784, 80, v); enqueue(0, 30, 0); enqueue(1047, 150, v); break;                     // "engage"
    case REGEN_END:   { static const uint16_t n[] = {523, 659, 784, 1047, 0, 784, 1047}; static const uint16_t d[] = {70, 70, 70, 70, 30, 70, 240};
                        for (int i = 0; i < 7; i++) enqueue(n[i], d[i], n[i] ? v : 0); } break;                 // rising "level up"
    case SOOT_AMBER:  enqueue(880, 100, v); enqueue(0, 60, 0); enqueue(880, 100, v); enqueue(0, 60, 0); enqueue(988, 170, v); break; // caution
    case SOOT_RED:    { static const uint16_t n[] = {1047, 0, 1047, 0, 880, 784, 698, 587}; static const uint16_t d[] = {70, 40, 70, 80, 150, 150, 150, 450};
                        for (int i = 0; i < 8; i++) enqueue(n[i], d[i], n[i] ? v : 0); } break;                 // two blips + falling "game over" feel
    case READY:       enqueue(700, 70, 30); enqueue(1000, 110, 30); break;                            // soft chirp
    case TEST:        enqueue(1000, 150, v); break;
  }
}

// Boot tunes: Mozart (public domain), transposed up an octave because this speaker is quiet below ~600 Hz.
// {hz, ms}; hz 0 = rest. At most 14 entries so they fit the 16-slot queue.
struct Note { uint16_t hz, ms; };
// Mozart (public domain), transposed up an octave: this speaker is quiet below ~600 Hz.
static const Note J4[] = {{988,90},{880,90},{831,90},{880,90},{1047,260},{1175,90},{1047,90},{988,90},{1047,90},{1319,300}};     // Rondo alla Turca, opening
static const Note J5[] = {{784,140},{0,25},{1175,140},{784,120},{0,25},{1175,140},{784,110},{1175,110},{784,110},{988,110},{1175,320}}; // Eine kleine Nachtmusik, opening
void audioJingle(uint8_t variant) {
  if (s_muted) return;
  const Note *tabs[] = {nullptr, J4, J5};   // 1 = Rondo alla Turca (chosen), 2 = Eine kleine Nachtmusik
  const size_t lens[] = {0, sizeof J4 / sizeof *J4, sizeof J5 / sizeof *J5};
  if (variant > 2) return;
  const Note *n = tabs[variant];
  size_t len = lens[variant];
  for (size_t i = 0; i < len; i++) enqueue(n[i].hz, n[i].ms, n[i].hz ? AUDIO_MAX_VOLUME_PCT : 0);
}

void audioSetMuted(bool m) { s_muted = m; }
bool audioIsMuted() { return s_muted; }
void audioToggleMute() {
  s_muted = !s_muted;
  if (s_muted) { enqueue(900, 70, 40); enqueue(600, 110, 40); }
  else         { enqueue(600, 70, 40); enqueue(900, 110, 40); }
}
