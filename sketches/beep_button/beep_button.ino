// beep_button — Freenove ESP32-S3 Dev Kit (FNK0086)
//
// Draws a BEEP button on the 2.8" ST7789 LCD.
// Tap it with your finger → speakers play a 880 Hz tone for 200 ms.
//
// Required libraries (installed automatically by flash script):
//   TFT_eSPI         — LCD driver
//   Arduino-FT6336U  — capacitive touch
//
// Pin summary:
//   LCD  : MOSI=20 SCLK=21 DC=0 CS=GND  (TFT_eSPI / HSPI)
//   Touch: SDA=2  SCL=1                  (FT6336U I2C)
//   Audio: BCLK=42 LRC=14 DOUT=41        (I2S)

#include <TFT_eSPI.h>
#include "FT6336U.h"
#include "driver/i2s_std.h"
#include <math.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define TOUCH_SDA  2
#define TOUCH_SCL  1

#define I2S_BCLK_PIN  42
#define I2S_LRC_PIN   14
#define I2S_DOUT_PIN  41

// ── Button layout (portrait 240×320) ─────────────────────────────────────────
#define SCREEN_W  240
#define SCREEN_H  320
#define BTN_W     160
#define BTN_H      80
#define BTN_X     ((SCREEN_W - BTN_W) / 2)   // 40
#define BTN_Y     ((SCREEN_H - BTN_H) / 2)   // 120
#define BTN_R      14   // corner radius

// ── Globals ───────────────────────────────────────────────────────────────────
TFT_eSPI tft;
FT6336U  ts(TOUCH_SDA, TOUCH_SCL, -1, -1);  // 'ts' avoids clash with TouchStatusEnum::touch

static i2s_chan_handle_t i2s_tx;

// ── I2S setup (new ESP-IDF 5.x API) ──────────────────────────────────────────
void i2s_setup() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chan_cfg, &i2s_tx, NULL);

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(22050),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK_PIN,
      .ws   = (gpio_num_t)I2S_LRC_PIN,
      .dout = (gpio_num_t)I2S_DOUT_PIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };
  i2s_channel_init_std_mode(i2s_tx, &std_cfg);
  i2s_channel_enable(i2s_tx);
}

// Play a sine-wave tone. freq_hz: pitch. duration_ms: length. vol: 0–32767.
void beep(int freq_hz, int duration_ms, int vol = 8000) {
  const int SR    = 22050;
  const int total = (SR * duration_ms) / 1000;
  const int CHUNK = 256;
  const int FADE  = 200;  // fade in/out samples — avoids clicks
  int16_t buf[CHUNK * 2];
  int done = 0;

  while (done < total) {
    int n = min(CHUNK, total - done);
    for (int i = 0; i < n; i++) {
      int pos = done + i;
      float env = 1.0f;
      if (pos < FADE)            env = (float)pos / FADE;
      else if (pos > total-FADE) env = (float)(total - pos) / FADE;
      float   t = (float)pos / SR;
      int16_t s = (int16_t)(vol * env * sinf(2.0f * M_PI * freq_hz * t));
      buf[i * 2]     = s;   // L
      buf[i * 2 + 1] = s;   // R
    }
    size_t written;
    i2s_channel_write(i2s_tx, buf, n * sizeof(int16_t) * 2, &written, pdMS_TO_TICKS(200));
    done += n;
  }
}

// ── Drawing ───────────────────────────────────────────────────────────────────
void draw_button(bool pressed) {
  uint16_t bg = pressed ? TFT_NAVY  : TFT_BLUE;
  uint16_t bd = pressed ? TFT_CYAN  : TFT_WHITE;

  tft.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, BTN_R, bg);
  tft.drawRoundRect(BTN_X,     BTN_Y,     BTN_W,     BTN_H,     BTN_R,   bd);
  tft.drawRoundRect(BTN_X + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, BTN_R-1, bd);

  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("BEEP", BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2);
}

void draw_screen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString("Tap the button", SCREEN_W / 2, 60);
  draw_button(false);
}

// ── Arduino entry points ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  draw_screen();

  ts.begin();
  Serial.printf("FT6336U firmware: %d  mode: %d\n",
    ts.read_firmware_id(), ts.read_device_mode());

  i2s_setup();
  Serial.println("Ready");
}

bool prev_hit = false;

void loop() {
  FT6336U_TouchPointType tp = ts.scan();

  bool hit = false;
  if (tp.touch_count > 0) {
    int tx = tp.tp[0].x;
    int ty = tp.tp[0].y;
    hit = (tx >= BTN_X && tx < BTN_X + BTN_W &&
           ty >= BTN_Y && ty < BTN_Y + BTN_H);
  }

  // Fire on leading edge only (finger down, not held)
  if (hit && !prev_hit) {
    draw_button(true);
    beep(880, 200);
    draw_button(false);
  }

  prev_hit = hit;
  delay(10);
}
