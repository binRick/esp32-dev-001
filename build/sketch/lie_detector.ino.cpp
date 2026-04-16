#include <Arduino.h>
#line 1 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
// lie_detector — Freenove ESP32-S3 Dev Kit (FNK0086)
//
// Lie Detector Simulator: funny yes/no questions, live BPM graph,
// bell for truth, buzzer for lying.
//
// Pin summary:
//   LCD   : TFT_eSPI  MOSI=20 SCLK=21 DC=0  CS=GND (ST7789 240×320)
//   Touch : FT6336U   SDA=2   SCL=1
//   HR    : MAX30102  SDA=2   SCL=1   addr=0x57 (shared I2C)
//   Audio : I2S       BCLK=42 LRC=14  DOUT=41

#include <TFT_eSPI.h>
#include "FT6336U.h"
#include "MAX30105.h"
#include "heartRate.h"
#include "driver/i2s_std.h"
#include <Wire.h>
#include <math.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
#define TOUCH_SDA  2
#define TOUCH_SCL  1
#define I2S_BCLK   42
#define I2S_LRC    14
#define I2S_DOUT   41

// ── Screen layout (240×320 portrait) ─────────────────────────────────────────
#define SW 240
#define SH 320

#define HDR_Y    0
#define HDR_H    28
#define BAR_Y    (HDR_H)
#define BAR_H    20
#define GFX_Y    (BAR_Y + BAR_H + 2)
#define GFX_H    90
#define GFX_X    8
#define GFX_W    (SW - 16)
#define Q_Y      (GFX_Y + GFX_H + 6)
#define Q_H      78
#define BTN_Y    (SH - 54)
#define BTN_H    46
#define BTN_W    104
#define YES_X    6
#define NO_X     (SW - BTN_W - 6)

// ── Colours ───────────────────────────────────────────────────────────────────
#define C_BG      TFT_BLACK
#define C_HDR_LIE 0xA000
#define C_HDR_OK  0x0340
#define C_HDR_CAL 0x000C
#define C_GBKG    0x0841
#define C_GRID    0x2104
#define C_LINE    0x07E0
#define C_YES_BG  0x0280
#define C_NO_BG   0x6000

// ── Questions ─────────────────────────────────────────────────────────────────
static const char* QUESTIONS[] = {
  "Have you ever blamed\na fart on your pet?",
  "Do you skip the gym\nand lie about going?",
  "Have you eaten food\nfrom someone else's\nfridge?",
  "Do you pretend to be\nasleep to avoid\ntalking to people?",
  "Have you regifted a\npresent you received?",
  "Do you lip-sync\nwhen you don't know\nthe lyrics?",
  "Have you Googled\nyourself recently?",
  "Do you laugh at jokes\nyou didn't understand?",
  "Have you faked being\nbusy to avoid plans?",
  "Do you talk to your\npets like they fully\nunderstand you?",
  "Have you sniffed food\nto check if it's\nstill edible?",
  "Have you pretended\nyou've seen a movie\nyou haven't?",
  "Do you check if the\ncoast is clear before\npicking your nose?",
  "Have you used 'seen'\nto avoid replying\nto a message?",
  "Do you narrate your\nlife in your head\nlike a movie?",
  "Have you ever lied\nabout your age?",
  "Do you silently judge\npeople by their\nfood orders?",
  "Have you ever taken\ncredit for someone\nelse's idea?",
  "Do you use your phone\nto avoid awkward\neye contact?",
  "Have you rewatched\na show without\ntelling anyone?",
};
static const int Q_COUNT = 20;

// ── State machine ─────────────────────────────────────────────────────────────
enum State { S_FINGER, S_CALIBRATE, S_QUESTION, S_VERDICT, S_THUMB_OFF };
volatile State gState = S_FINGER;
State resumeState = S_FINGER;   // where to return after thumb replaced

// ── Heart rate ────────────────────────────────────────────────────────────────
MAX30105 hr;
static const byte RATE_HIST = 8;
byte   rateHistory[RATE_HIST];
byte   rateSpot = 0;
long   lastBeatMs = 0;
int    beatAvg = 0;
bool   fingerOn = false;
long   gLastIR  = 0;   // raw IR reading, updated every FIFO drain

// ── Graph ─────────────────────────────────────────────────────────────────────
static uint8_t gBuf[GFX_W];   // circular, each entry = BPM (0 = no data)
static int     gHead = 0;

// ── Game state ────────────────────────────────────────────────────────────────
float  baselineBpm    = 72.0f;
int    peakBpm        = 72;
int    qIndex         = 0;
int    shuffled[Q_COUNT];
bool   isLying        = false;
unsigned long verdictAt = 0;

// ── I2S audio ─────────────────────────────────────────────────────────────────
static i2s_chan_handle_t i2sTx;
enum SndCmd { SND_NONE, SND_BELL, SND_BUZZ };
volatile SndCmd gSound = SND_NONE;

#line 115 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void i2s_setup();
#line 135 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
static void writeI2S(int16_t* buf, int n);
#line 141 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void playBell();
#line 159 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void playBuzzer();
#line 182 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void audioTask(void*);
#line 199 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void drawHeader(const char* txt, uint16_t bg);
#line 207 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void drawBpmBar(int bpm, bool valid);
#line 225 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void drawGraph();
#line 258 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void pushSample(int bpm);
#line 263 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void drawQuestion(int qi);
#line 299 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void showThumbOffScreen();
#line 316 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void showQuestionScreen();
#line 328 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void shuffleQs();
#line 336 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
bool detectLie(float baseline, int peak);
#line 345 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void updateHR();
#line 374 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void setup();
#line 448 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void loop();
#line 115 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/lie_detector/lie_detector.ino"
void i2s_setup() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&cc, &i2sTx, NULL);
  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(22050),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false },
    },
  };
  i2s_channel_init_std_mode(i2sTx, &sc);
  i2s_channel_enable(i2sTx);
}

static void writeI2S(int16_t* buf, int n) {
  size_t wr;
  // Short timeout so millis()-based cutoffs can fire promptly
  i2s_channel_write(i2sTx, buf, n * 4, &wr, pdMS_TO_TICKS(20));
}

void playBell() {
  // 1320 Hz decaying sine, runs for exactly 2 real seconds
  const int SR = 22050, CHUNK = 256;
  int16_t buf[CHUNK * 2];
  unsigned long endMs = millis() + 2000;
  long i = 0;
  while (millis() < endMs) {
    for (int j = 0; j < CHUNK; j++) {
      float t   = (float)(i + j) / SR;
      float env = expf(-5.5f * t);
      int16_t s = (int16_t)(22000 * env * sinf(6.2832f * 1320.0f * t));
      buf[j * 2] = buf[j * 2 + 1] = s;
    }
    writeI2S(buf, CHUNK);
    i += CHUNK;
  }
}

void playBuzzer() {
  // Pulsed dual-frequency square wave, runs for exactly 2 real seconds
  const int SR = 22050, CHUNK = 256;
  int16_t buf[CHUNK * 2];
  unsigned long endMs = millis() + 2000;
  long i = 0;
  while (millis() < endMs) {
    for (int j = 0; j < CHUNK; j++) {
      float t    = (float)(i + j) / SR;
      float freq = (fmodf(t, 0.2f) < 0.1f) ? 180.0f : 350.0f;
      bool  on   = fmodf(t, 0.13f) < 0.09f;
      float ph   = fmodf(t * freq, 1.0f);
      int16_t s  = on ? (ph < 0.5f ? 11000 : -11000) : 0;
      buf[j * 2] = buf[j * 2 + 1] = s;
    }
    writeI2S(buf, CHUNK);
    i += CHUNK;
  }
  // Flush silence to clear DMA buffer and stop audio immediately
  memset(buf, 0, sizeof(buf));
  for (int f = 0; f < 8; f++) writeI2S(buf, CHUNK);
}

void audioTask(void*) {
  while (true) {
    SndCmd cmd = gSound;
    if (cmd != SND_NONE) {
      gSound = SND_NONE;
      if (cmd == SND_BELL) playBell();
      else                  playBuzzer();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ── Peripherals ───────────────────────────────────────────────────────────────
TFT_eSPI tft;
FT6336U  ts(TOUCH_SDA, TOUCH_SCL, -1, -1);

// ── Drawing ───────────────────────────────────────────────────────────────────
void drawHeader(const char* txt, uint16_t bg) {
  tft.fillRect(0, HDR_Y, SW, HDR_H, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString(txt, SW / 2, HDR_Y + HDR_H / 2);
}

void drawBpmBar(int bpm, bool valid) {
  tft.fillRect(0, BAR_Y, SW, BAR_H, 0x0841);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  if (valid && bpm > 20) {
    char buf[16];
    snprintf(buf, sizeof(buf), " %3d BPM", bpm);
    uint16_t c = (bpm > 105) ? TFT_RED : (bpm > 85) ? TFT_ORANGE : TFT_GREEN;
    tft.setTextColor(c, 0x0841);
    tft.drawString(buf, 0, BAR_Y + BAR_H / 2);
    int w = map(bpm < 50 ? 50 : (bpm > 140 ? 140 : bpm), 50, 140, 0, SW - 58);
    tft.fillRect(54, BAR_Y + 3, w, BAR_H - 6, c);
  } else {
    tft.setTextColor(TFT_DARKGREY, 0x0841);
    tft.drawString(" -- no thumb --", 0, BAR_Y + BAR_H / 2);
  }
}

void drawGraph() {
  tft.fillRect(GFX_X, GFX_Y, GFX_W, GFX_H, C_GBKG);
  // Grid lines
  for (int y = GFX_Y + GFX_H / 3; y < GFX_Y + GFX_H; y += GFX_H / 3)
    tft.drawFastHLine(GFX_X, y, GFX_W, C_GRID);
  // BPM scale labels
  tft.setTextFont(1);
  tft.setTextColor(0x4208, C_GBKG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("120", GFX_X - 1, GFX_Y + 4);
  tft.drawString("85",  GFX_X - 1, GFX_Y + GFX_H / 2);
  tft.drawString("50",  GFX_X - 1, GFX_Y + GFX_H - 4);
  // Plot line
  int prev_py = -1;
  for (int i = 0; i < GFX_W; i++) {
    int idx = (gHead + i) % GFX_W;
    if (gBuf[idx] == 0) { prev_py = -1; continue; }
    int bpm = gBuf[idx];
    int py  = GFX_Y + GFX_H - 1
              - map(bpm < 50 ? 50 : (bpm > 120 ? 120 : bpm), 50, 120, 0, GFX_H - 2);
    int px  = GFX_X + i;
    if (prev_py >= 0) {
      tft.drawLine(px - 1, prev_py, px, py, C_LINE);
      // glow pixel above/below for drama
      tft.drawPixel(px, py - 1, 0x03E0);
      tft.drawPixel(px, py + 1, 0x03E0);
    } else {
      tft.drawPixel(px, py, C_LINE);
    }
    prev_py = py;
  }
}

void pushSample(int bpm) {
  gBuf[gHead] = (uint8_t)(bpm < 0 ? 0 : (bpm > 255 ? 255 : bpm));
  gHead = (gHead + 1) % GFX_W;
}

void drawQuestion(int qi) {
  tft.fillRect(0, Q_Y, SW, Q_H, C_BG);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  char tmp[96];
  strncpy(tmp, QUESTIONS[qi], sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  int y = Q_Y + 6;
  char* line = strtok(tmp, "\n");
  while (line && y < Q_Y + Q_H - 10) {
    tft.drawString(line, SW / 2, y);
    y += 18;
    line = strtok(NULL, "\n");
  }
}

void drawButtons(bool hiY = false, bool hiN = false) {
  // YES
  uint16_t yc = hiY ? TFT_GREEN : C_YES_BG;
  tft.fillRoundRect(YES_X, BTN_Y, BTN_W, BTN_H, 8, yc);
  tft.drawRoundRect(YES_X, BTN_Y, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, yc);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("YES", YES_X + BTN_W / 2, BTN_Y + BTN_H / 2);
  // NO
  uint16_t nc = hiN ? TFT_RED : C_NO_BG;
  tft.fillRoundRect(NO_X, BTN_Y, BTN_W, BTN_H, 8, nc);
  tft.drawRoundRect(NO_X, BTN_Y, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, nc);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("NO", NO_X + BTN_W / 2, BTN_Y + BTN_H / 2);
}

void showThumbOffScreen() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("THUMB OFF", SW / 2, 100);
  tft.drawString("SENSOR!", SW / 2, 135);
  tft.setTextFont(2);
  tft.drawString("Put your thumb back", SW / 2, 195);
  tft.drawString("on the sensor", SW / 2, 215);
  tft.drawString("(red dot, left side)", SW / 2, 240);
  tft.setTextFont(2);
  tft.setTextColor(TFT_YELLOW, TFT_RED);
  tft.drawString("Removing your thumb is", SW / 2, 275);
  tft.drawString("very suspicious.", SW / 2, 295);
}

void showQuestionScreen() {
  tft.fillScreen(C_BG);
  drawHeader("   DETECTING LIES...   ", C_HDR_LIE);
  memset(gBuf, 0, sizeof(gBuf));
  gHead = 0;
  drawGraph();
  drawBpmBar(beatAvg, fingerOn && beatAvg > 20);
  drawQuestion(shuffled[qIndex % Q_COUNT]);
  drawButtons();
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void shuffleQs() {
  for (int i = 0; i < Q_COUNT; i++) shuffled[i] = i;
  for (int i = Q_COUNT - 1; i > 0; i--) {
    int j = (int)(esp_random() % (i + 1));
    int t = shuffled[i]; shuffled[i] = shuffled[j]; shuffled[j] = t;
  }
}

bool detectLie(float baseline, int peak) {
  // First two questions are always truth (warm-up / trust builder)
  if (qIndex < 2) return false;
  // After that, pure 50/50
  return (esp_random() % 2) == 0;
}

// ── Heart rate update (call every loop tick) ──────────────────────────────────
// Drains the full FIFO each call so no beats are missed during TFT drawing.
void updateHR() {
  hr.check();
  if (!hr.available()) return;

  long lastIR = 0;
  while (hr.available()) {
    lastIR = hr.getFIFOIR();
    fingerOn = (lastIR >= 50000);
    gLastIR  = lastIR;

    if (fingerOn && checkForBeat(lastIR)) {
      long now   = millis();
      long delta = now - lastBeatMs;
      lastBeatMs = now;
      float bpm  = 60000.0f / (float)delta;
      if (bpm > 20 && bpm < 255) {
        rateHistory[rateSpot % RATE_HIST] = (byte)bpm;
        rateSpot++;
        int sum = 0, cnt = 0;
        for (int i = 0; i < RATE_HIST; i++) if (rateHistory[i]) { sum += rateHistory[i]; cnt++; }
        beatAvg = cnt ? sum / cnt : 0;
      }
    }
    hr.nextSample();
  }
  if (!fingerOn) beatAvg = 0;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  ts.begin();

  if (!hr.begin(Wire, I2C_SPEED_FAST)) {
    tft.setTextColor(TFT_RED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.drawString("MAX30102 not found!", SW / 2, SH / 2);
    tft.drawString("Check wiring (SDA=2 SCL=1)", SW / 2, SH / 2 + 20);
    while (true) delay(100);
  }
  hr.setup(0x7F, 4, 2, 100, 411, 4096);  // 0x7F = brighter LED, better thumb signal
  hr.setPulseAmplitudeRed(0x0A);
  hr.setPulseAmplitudeGreen(0);

  i2s_setup();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 2, NULL, 0);

  memset(gBuf, 0, sizeof(gBuf));
  memset(rateHistory, 0, sizeof(rateHistory));
  shuffleQs();

  // Welcome splash
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, C_BG);
  tft.setTextFont(4);
  tft.drawString("LIE DETECTOR", SW / 2, 70);
  tft.drawString("3000", SW / 2, 105);
  tft.setTextFont(4);
  tft.setTextColor(TFT_YELLOW, C_BG);
  tft.drawString("This device will", SW / 2, 170);
  tft.drawString("detect every lie.", SW / 2, 205);
  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, C_BG);
  tft.drawString("Resistance is futile.", SW / 2, 255);
  delay(2200);

  // Finger prompt
  tft.fillScreen(C_BG);
  drawHeader("  LIE DETECTOR 3000  ", C_HDR_LIE);
  tft.setTextColor(TFT_YELLOW, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("Place your", SW / 2, 100);
  tft.drawString("thumb on the", SW / 2, 130);
  tft.drawString("truth sensor", SW / 2, 160);
  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, C_BG);
  tft.drawString("(sensor on the left — red dot)", SW / 2, 200);
  tft.drawString("We need your pulse to", SW / 2, 225);
  tft.drawString("catch you in a lie.", SW / 2, 245);

  gState = S_FINGER;
}

// ── Loop state variables ───────────────────────────────────────────────────────
static unsigned long lastGraphMs  = 0;
static unsigned long lastBlinkMs  = 0;
static bool          blinkOn      = false;
static unsigned long calibStart   = 0;
static float         calibSum     = 0;
static int           calibCount   = 0;
static bool          prevTouch    = false;
#define CALIB_MS 3000

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  updateHR();

  FT6336U_TouchPointType tp = ts.scan();
  bool touched = (tp.touch_count > 0);
  int  tx = touched ? tp.tp[0].x : -1;
  int  ty = touched ? tp.tp[0].y : -1;
  bool freshTouch = touched && !prevTouch;

  switch (gState) {

    // ── Wait for finger ────────────────────────────────────────────────────────
    case S_FINGER: {
      if (fingerOn) {
        calibStart = millis();
        calibSum   = 0;
        calibCount = 0;
        peakBpm    = beatAvg > 0 ? beatAvg : 72;
        // Keep rateHistory/beatAvg — beats may already be locked in
        gState     = S_CALIBRATE;
        tft.fillScreen(C_BG);
        drawHeader("  CALIBRATING...  ", C_HDR_CAL);
        tft.setTextColor(TFT_CYAN, C_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.drawString("Hold very still...", SW / 2, 90);
        tft.drawString("Recording your NORMAL", SW / 2, 115);
        tft.drawString("heart rate so we know", SW / 2, 135);
        tft.drawString("when you're LYING.", SW / 2, 155);
      } else {
        // Live sensor indicator — update every 150ms
        static unsigned long sensorMs = 0;
        if (millis() - sensorMs > 150) {
          sensorMs = millis();

          long ir = gLastIR;
          // Signal strength bar (IR typically 0..100000+)
          int sigPct = (int)(ir / 1200);   // 0..~100
          if (sigPct > 100) sigPct = 100;

          // Status label + colour
          const char* sigLabel;
          uint16_t    sigCol;
          if (ir < 5000)        { sigLabel = "NO SIGNAL";      sigCol = TFT_DARKGREY; }
          else if (ir < 30000)  { sigLabel = "WEAK SIGNAL";    sigCol = TFT_ORANGE;   }
          else if (ir < 50000)  { sigLabel = "ALMOST THERE...";sigCol = TFT_YELLOW;   }
          else                  { sigLabel = "THUMB DETECTED"; sigCol = TFT_GREEN;    }  // shouldn't reach

          // Clear indicator area
          tft.fillRect(10, 262, SW - 20, 50, C_BG);

          // Bar background + fill
          tft.fillRect(10, 268, SW - 20, 14, 0x1082);
          int barW = (sigPct * (SW - 20)) / 100;
          if (barW > 0) tft.fillRect(10, 268, barW, 14, sigCol);

          // Status text
          tft.setTextColor(sigCol, C_BG);
          tft.setTextDatum(MC_DATUM);
          tft.setTextFont(2);
          tft.drawString(sigLabel, SW / 2, 295);

          // Raw IR for debugging (small, dim)
          char irBuf[20];
          snprintf(irBuf, sizeof(irBuf), "IR: %ld", ir);
          tft.setTextColor(0x4208, C_BG);
          tft.setTextFont(1);
          tft.drawString(irBuf, SW / 2, 312);
        }
      }
      break;
    }

    // ── Collect baseline ───────────────────────────────────────────────────────
    case S_CALIBRATE: {
      if (!fingerOn) {
        resumeState = S_CALIBRATE;
        gState = S_THUMB_OFF;
        showThumbOffScreen();
        break;
      }
      unsigned long elapsed = millis() - calibStart;
      if (beatAvg > 20) { calibSum += beatAvg; calibCount++; }

      if (millis() - lastGraphMs > 350) {
        lastGraphMs = millis();
        drawBpmBar(beatAvg, true);
        // Progress bar — 80px wide, centred
        int barW  = 80;
        int barX  = (SW - barW) / 2;
        int pw    = constrain((int)map((long)elapsed, 0L, (long)CALIB_MS, 0L, (long)barW), 0, barW);
        tft.fillRect(barX, 195, barW, 10, 0x0841);
        tft.fillRect(barX, 195, pw,   10, TFT_CYAN);
        // BPM display
        tft.setTextColor(TFT_WHITE, C_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(4);
        if (beatAvg > 20) {
          char buf[20]; snprintf(buf, sizeof(buf), "BPM: %d", beatAvg);
          tft.drawString(buf, SW / 2, 220);
        } else {
          tft.drawString("Measuring...", SW / 2, 220);
        }
      }

      if (elapsed >= CALIB_MS) {
        baselineBpm = calibCount > 0 ? calibSum / calibCount : 72.0f;
        peakBpm     = (int)baselineBpm;
        gState      = S_QUESTION;
        showQuestionScreen();
      }
      break;
    }

    // ── Show question + live graph ─────────────────────────────────────────────
    case S_QUESTION: {
      if (!fingerOn) {
        resumeState = S_QUESTION;
        gState = S_THUMB_OFF;
        showThumbOffScreen();
        break;
      }
      if (millis() - lastGraphMs > 300) {
        lastGraphMs = millis();
        pushSample(fingerOn && beatAvg > 20 ? beatAvg : 0);
        if (fingerOn && beatAvg > peakBpm) peakBpm = beatAvg;
        drawGraph();
        drawBpmBar(beatAvg, fingerOn && beatAvg > 20);
        // Blink header
        if (millis() - lastBlinkMs > 900) {
          lastBlinkMs = millis();
          blinkOn = !blinkOn;
          drawHeader(blinkOn ? " !! TELL THE TRUTH !! " : "   DETECTING LIES...  ",
                     blinkOn ? 0xB800 : C_HDR_LIE);
        }
      }
      if (freshTouch) {
        bool pY = (tx >= YES_X && tx < YES_X + BTN_W && ty >= BTN_Y && ty < BTN_Y + BTN_H);
        bool pN = (tx >= NO_X  && tx < NO_X  + BTN_W && ty >= BTN_Y && ty < BTN_Y + BTN_H);
        if (pY || pN) {
          drawButtons(pY, pN);
          delay(180);
          isLying   = detectLie(baselineBpm, peakBpm);
          gSound    = isLying ? SND_BUZZ : SND_BELL;
          verdictAt = millis();
          gState    = S_VERDICT;

          // Verdict screen
          tft.fillScreen(C_BG);
          if (isLying) {
            tft.fillScreen(0x2800);
            drawHeader("  *** L I A R ! ***  ", TFT_RED);
            tft.setTextColor(TFT_RED, 0x2800);
            tft.setTextDatum(MC_DATUM);
            tft.setTextFont(7);
            tft.drawString("LIE!", SW / 2, 145);
            tft.setTextFont(2);
            tft.setTextColor(TFT_ORANGE, 0x2800);
            static const char* lMsgs[] = {
              "Your pulse gave\nyou away!",
              "The machine\nnever lies!",
              "Pinocchio detected!\nNose check needed.",
              "Nice try, cheater!\nWe all saw it.",
              "Your heart knows\nthe truth!",
            };
            const char* m = lMsgs[(int)(esp_random() % 5)];
            char ltmp[80]; strncpy(ltmp, m, 79);
            int ly = 220;
            char* ll = strtok(ltmp, "\n");
            while (ll && ly < 280) { tft.drawString(ll, SW/2, ly); ly += 20; ll = strtok(NULL,"\n"); }
          } else {
            tft.fillScreen(0x0120);
            drawHeader("  *** T R U T H ! ***  ", 0x0400);
            tft.setTextColor(TFT_GREEN, 0x0120);
            tft.setTextDatum(MC_DATUM);
            tft.setTextFont(7);
            tft.drawString("TRUE", SW / 2, 145);
            tft.setTextFont(2);
            tft.setTextColor(TFT_CYAN, 0x0120);
            static const char* tMsgs[] = {
              "A rare honest human!\nFrame this moment.",
              "Steady pulse. Or...\nyou're good at lying.",
              "We believe you.\nFor now.",
              "Congratulations.\nApparently.",
              "Heart rate normal.\nSuspiciously normal.",
            };
            const char* m = tMsgs[(int)(esp_random() % 5)];
            char ttmp[80]; strncpy(ttmp, m, 79);
            int ty2 = 220;
            char* tl = strtok(ttmp, "\n");
            while (tl && ty2 < 280) { tft.drawString(tl, SW/2, ty2); ty2 += 20; tl = strtok(NULL,"\n"); }
          }
          tft.setTextFont(4);
          tft.setTextColor(TFT_WHITE, isLying ? 0x2800 : 0x0120);
          tft.drawString("Tap screen for", SW / 2, 290);
          tft.drawString("next question", SW / 2, 312);
        }
      }
      break;
    }

    // ── Verdict — wait for tap ─────────────────────────────────────────────────
    case S_VERDICT:
      if (!fingerOn) {
        resumeState = S_VERDICT;
        gState = S_THUMB_OFF;
        showThumbOffScreen();
        break;
      }
      if (freshTouch && (millis() - verdictAt > 1800)) {
        qIndex++;
        peakBpm = (int)baselineBpm;
        blinkOn = false;
        gState  = S_QUESTION;
        showQuestionScreen();
      }
      break;

    // ── Thumb removed — flash red, wait for thumb back ─────────────────────────
    case S_THUMB_OFF: {
      // Flash background between red and dark red
      static unsigned long flashMs = 0;
      static bool flashHi = true;
      if (millis() - flashMs > 400) {
        flashMs = millis();
        flashHi = !flashHi;
        tft.fillRect(0, 0, SW, HDR_H + 10, flashHi ? TFT_RED : 0x6000);
      }
      if (fingerOn) {
        // Thumb back — resume previous state
        if (resumeState == S_CALIBRATE) {
          // Restart calibration cleanly
          calibStart = millis();
          calibSum   = 0;
          calibCount = 0;
          peakBpm    = beatAvg;
          gState     = S_CALIBRATE;
          tft.fillScreen(C_BG);
          drawHeader("  CALIBRATING...  ", C_HDR_CAL);
          tft.setTextColor(TFT_CYAN, C_BG);
          tft.setTextDatum(MC_DATUM);
          tft.setTextFont(2);
          tft.drawString("Hold very still...", SW / 2, 90);
          tft.drawString("Recording your NORMAL", SW / 2, 115);
          tft.drawString("heart rate so we know", SW / 2, 135);
          tft.drawString("when you're LYING.", SW / 2, 155);
        } else if (resumeState == S_QUESTION) {
          peakBpm = (int)baselineBpm;
          gState  = S_QUESTION;
          showQuestionScreen();
        } else {
          // S_VERDICT — redraw verdict screen isn't trivial; just go to next Q
          qIndex++;
          peakBpm = (int)baselineBpm;
          blinkOn = false;
          gState  = S_QUESTION;
          showQuestionScreen();
        }
      }
      break;
    }
  }

  prevTouch = touched;
  delay(6);
}

