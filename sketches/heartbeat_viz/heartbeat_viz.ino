// heartbeat_viz — Freenove ESP32-S3 Dev Kit (FNK0086)
//
// Live ECG-style heartbeat visualizer using MAX30102 IR signal.
//
// Features:
//   • Scrolling waveform (AC-coupled IR signal, auto-scaled)
//   • ECG grid background
//   • Full-screen red flash on each detected beat
//   • Animated pixel-art heart that pumps on each beat
//   • BPM in large digits, color-coded by rate
//   • "LIVE" blink dot to confirm sensor is reading
//   • Sprite-based rendering for zero flicker
//   • Touch to freeze / unfreeze the trace
//
// Pin summary:
//   LCD   : TFT_eSPI  MOSI=20 SCLK=21 DC=0  CS=GND (ST7789 240×320)
//   Touch : FT6336U   SDA=2   SCL=1
//   HR    : MAX30102  SDA=2   SCL=1   addr=0x57 (shared I2C)

#include <TFT_eSPI.h>
#include "FT6336U.h"
#include "MAX30105.h"
#include "heartRate.h"
#include <Wire.h>
#include <math.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
#define TOUCH_SDA  2
#define TOUCH_SCL  1

// ── Screen dimensions ─────────────────────────────────────────────────────────
#define SW  240
#define SH  320

// ── Layout ────────────────────────────────────────────────────────────────────
// Top area: BPM + animated heart
#define TOP_H      56   // height of header panel
// Waveform area
#define WAVE_Y     TOP_H
#define WAVE_H     (SH - TOP_H)  // 264 px tall
#define WAVE_W     SW

// ── Colours ───────────────────────────────────────────────────────────────────
#define C_BG        TFT_BLACK
#define C_GRID      0x0841   // very dark green grid
#define C_GRID2     0x0421   // lighter grid accent
#define C_TRACE     0x07E0   // bright green
#define C_TRACE_DIM 0x03E0   // glow shadow
#define C_FLASH_HI  0xF800   // full red flash
#define C_FLASH_MID 0x9000   // mid flash frame
#define C_FLASH_LO  0x3000   // fade-out frame
#define C_HDR_BG    0x0841
#define C_LIVE_ON   0x07E0
#define C_LIVE_OFF  0x0421

// ── BPM colour thresholds ─────────────────────────────────────────────────────
inline uint16_t bpmColor(int bpm) {
  if (bpm <= 0)   return TFT_DARKGREY;
  if (bpm < 55)   return 0x001F;  // blue — bradycardia
  if (bpm < 85)   return 0x07E0;  // green — normal
  if (bpm < 100)  return 0xFFE0;  // yellow — elevated
  return 0xF800;                   // red — high
}

// ── Waveform ring buffer ───────────────────────────────────────────────────────
// Store raw AC-coupled IR values (float, normalized to –1..+1) for display.
// We keep WAVE_W samples (one per pixel column).
#define BUF_LEN  WAVE_W
static float   waveBuf[BUF_LEN];
static int     waveBufHead = 0;   // next write position

// ── DC baseline tracking (slow exponential moving average) ────────────────────
static float dcBaseline = 0.0f;
#define DC_ALPHA  0.02f   // ~50-sample time constant

// ── Auto-scale: running max of |AC| over last N samples ───────────────────────
static float acPeak = 500.0f;   // starts at a safe default
#define PEAK_DECAY  0.998f      // slow decay so scale doesn't jitter

// ── Heart rate ────────────────────────────────────────────────────────────────
MAX30105  hr;
static const byte RATE_HIST = 8;
byte  rateHistory[RATE_HIST];
byte  rateSpot   = 0;
long  lastBeatMs = 0;
int   beatAvg    = 0;
bool  fingerOn   = false;
bool  beatNow    = false;   // true for one loop tick on each beat

// ── Beat flash state ──────────────────────────────────────────────────────────
// 0=idle, 1=hi flash, 2=mid, 3=lo — each lasts one draw frame
static int  flashState = 0;
static unsigned long flashMs = 0;
#define FLASH_FRAME_MS  55

// ── Animated heart ────────────────────────────────────────────────────────────
// Pixel-art 11×9 heart bitmap (1=filled, 0=empty)
static const uint8_t HEART_MAP[9][11] = {
  {0,1,1,0,0,0,0,0,1,1,0},
  {1,1,1,1,0,0,0,1,1,1,1},
  {1,1,1,1,1,0,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,1,0},
  {0,0,1,1,1,1,1,1,1,0,0},
  {0,0,0,1,1,1,1,1,0,0,0},
  {0,0,0,0,1,1,1,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0},
};
static int   heartScale  = 2;   // 1=small, 3=big — animated
static bool  heartBig    = false;
#define HEART_COLS  11
#define HEART_ROWS  9
#define HEART_X     (SW - HEART_COLS*3 - 6)  // right side of header
#define HEART_Y     8

// ── Live dot blink ────────────────────────────────────────────────────────────
static bool liveOn = false;
static unsigned long liveMs = 0;
#define LIVE_PERIOD_MS  500

// ── Freeze toggle (touch) ─────────────────────────────────────────────────────
static bool frozen = false;
static bool prevTouch = false;

// ── Peripherals ───────────────────────────────────────────────────────────────
TFT_eSPI    tft;
TFT_eSprite wavSprite(&tft);   // full-width waveform sprite
FT6336U     ts(TOUCH_SDA, TOUCH_SCL, -1, -1);

// ── Draw a pixel-art heart at (x,y) with given pixel size and colour ──────────
void drawHeart(int x, int y, int sz, uint16_t col, uint16_t bg) {
  for (int r = 0; r < HEART_ROWS; r++) {
    for (int c = 0; c < HEART_COLS; c++) {
      uint16_t px = HEART_MAP[r][c] ? col : bg;
      tft.fillRect(x + c*sz, y + r*sz, sz, sz, px);
    }
  }
}

// ── Draw top header panel ─────────────────────────────────────────────────────
void drawHeader(uint16_t flashBg) {
  uint16_t bg = (flashBg != 0) ? flashBg : C_HDR_BG;
  tft.fillRect(0, 0, SW, TOP_H, bg);

  // BPM
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(7);   // 48-px digits
  uint16_t bCol = (flashBg != 0) ? TFT_WHITE : bpmColor(beatAvg);
  tft.setTextColor(bCol, bg);
  char bpmStr[8];
  if (fingerOn && beatAvg > 20)
    snprintf(bpmStr, sizeof(bpmStr), "%3d", beatAvg);
  else
    snprintf(bpmStr, sizeof(bpmStr), " --");
  tft.drawString(bpmStr, 6, 28);

  // "BPM" label
  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, bg);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("BPM", 6, TOP_H - 4);

  // FROZEN indicator
  if (frozen) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_YELLOW, bg);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("[ FROZEN ]", SW/2, TOP_H - 4);
  }

  // LIVE blink dot
  tft.fillCircle(HEART_X - 12, TOP_H/2, 4,
                 (liveOn && fingerOn) ? C_LIVE_ON : (bg == C_HDR_BG ? C_LIVE_OFF : bg));

  // Animated heart
  int sz = heartBig ? 3 : 2;
  uint16_t hCol = (flashBg != 0) ? 0xFBEF : (fingerOn ? 0xF800 : 0x4208);
  drawHeart(HEART_X, HEART_Y, sz, hCol, bg);
}

// ── Draw ECG grid into the sprite ─────────────────────────────────────────────
void drawGrid() {
  wavSprite.fillSprite(C_BG);
  // Major horizontal lines every 66 px (WAVE_H/4)
  for (int y = WAVE_H/4; y < WAVE_H; y += WAVE_H/4)
    wavSprite.drawFastHLine(0, y, WAVE_W, C_GRID);
  // Minor horizontal lines halfway between majors
  for (int y = WAVE_H/8; y < WAVE_H; y += WAVE_H/4)
    wavSprite.drawFastHLine(0, y, WAVE_W, C_GRID2);
  // Major vertical lines every 60 px
  for (int x = 60; x < WAVE_W; x += 60)
    wavSprite.drawFastVLine(x, 0, WAVE_H, C_GRID);
  // Minor vertical lines every 20 px
  for (int x = 20; x < WAVE_W; x += 20) {
    if (x % 60 != 0)
      wavSprite.drawFastVLine(x, 0, WAVE_H, C_GRID2);
  }
}

// ── Map a normalised AC value (–1..+1) to a pixel y within the sprite ─────────
inline int acToY(float v) {
  // Centre at WAVE_H/2, full deflection = 80% of half-height
  float half = (WAVE_H - 2) * 0.40f;
  int y = (int)(WAVE_H/2 - v * half);
  if (y < 1)        y = 1;
  if (y > WAVE_H-2) y = WAVE_H-2;
  return y;
}

// ── Render waveform into sprite and push to screen ────────────────────────────
void drawWave() {
  drawGrid();

  // Trace colour dimming at leading edge for "cursor sweep" effect
  int prevY = -1;
  for (int col = 0; col < WAVE_W; col++) {
    int idx = (waveBufHead + col) % BUF_LEN;
    float v = waveBuf[idx];
    if (v == 0.0f && !fingerOn) { prevY = -1; continue; }
    int y = acToY(v);

    // Brightness fades out near the leading edge (last 8 columns = cursor)
    bool isCursor = (col >= WAVE_W - 8);
    uint16_t col1 = isCursor ? 0x0180 : C_TRACE;
    uint16_t col2 = isCursor ? 0x00C0 : C_TRACE_DIM;

    if (prevY >= 0) {
      wavSprite.drawLine(col-1, prevY, col, y, col1);
      // glow
      if (y > 1)        wavSprite.drawPixel(col, y-1, col2);
      if (y < WAVE_H-1) wavSprite.drawPixel(col, y+1, col2);
    } else {
      wavSprite.drawPixel(col, y, col1);
    }
    prevY = y;
  }

  wavSprite.pushSprite(0, WAVE_Y);
}

// ── HR update — drain full FIFO ───────────────────────────────────────────────
void updateHR() {
  hr.check();
  if (!hr.available()) return;

  beatNow = false;
  while (hr.available()) {
    long ir = hr.getFIFOIR();
    fingerOn = (ir >= 50000);

    if (fingerOn) {
      // Update DC baseline
      dcBaseline = dcBaseline * (1.0f - DC_ALPHA) + (float)ir * DC_ALPHA;
      float ac = (float)ir - dcBaseline;

      // Update auto-scale
      float absAc = fabsf(ac);
      if (absAc > acPeak) acPeak = absAc;
      else                acPeak *= PEAK_DECAY;
      if (acPeak < 100.0f) acPeak = 100.0f;  // floor

      float norm = ac / acPeak;  // –1..+1

      if (!frozen) {
        waveBuf[waveBufHead] = norm;
        waveBufHead = (waveBufHead + 1) % BUF_LEN;
      }

      // Beat detection
      if (checkForBeat(ir)) {
        long now   = millis();
        long delta = now - lastBeatMs;
        lastBeatMs = now;
        float bpm  = 60000.0f / (float)delta;
        if (bpm > 20.0f && bpm < 250.0f) {
          rateHistory[rateSpot % RATE_HIST] = (byte)bpm;
          rateSpot++;
          int sum = 0, cnt = 0;
          for (int i = 0; i < RATE_HIST; i++)
            if (rateHistory[i]) { sum += rateHistory[i]; cnt++; }
          beatAvg = cnt ? sum / cnt : 0;
        }
        beatNow = true;
      }
    } else {
      // No finger — push zeros to show flatline
      if (!frozen) {
        waveBuf[waveBufHead] = 0.0f;
        waveBufHead = (waveBufHead + 1) % BUF_LEN;
      }
      beatAvg = 0;
      dcBaseline = 0.0f;
    }

    hr.nextSample();
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  ts.begin();

  // Create waveform sprite
  wavSprite.createSprite(WAVE_W, WAVE_H);
  wavSprite.setColorDepth(16);

  // Init MAX30102
  if (!hr.begin(Wire, I2C_SPEED_FAST)) {
    tft.setTextColor(TFT_RED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.drawString("MAX30102 not found!", SW/2, SH/2);
    tft.drawString("Check SDA=2 SCL=1",  SW/2, SH/2 + 20);
    while (true) delay(100);
  }
  hr.setup(0x7F, 4, 2, 100, 411, 4096);
  hr.setPulseAmplitudeRed(0x0A);
  hr.setPulseAmplitudeGreen(0);

  memset(rateHistory, 0, sizeof(rateHistory));
  memset(waveBuf, 0, sizeof(waveBuf));

  // Splash screen
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_RED, C_BG);
  tft.setTextFont(4);
  tft.drawString("HEARTBEAT", SW/2, 100);
  tft.drawString("VISUALIZER", SW/2, 130);
  tft.setTextFont(2);
  tft.setTextColor(TFT_DARKGREY, C_BG);
  tft.drawString("Place finger on sensor", SW/2, 190);
  tft.drawString("Tap screen to freeze", SW/2, 210);
  delay(2000);

  tft.fillScreen(C_BG);
  drawHeader(0);
  drawWave();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
static unsigned long lastDrawMs = 0;
#define DRAW_INTERVAL_MS  30   // ~33 fps

void loop() {
  updateHR();

  // Touch — toggle freeze
  FT6336U_TouchPointType tp = ts.scan();
  bool touched = (tp.touch_count > 0);
  if (touched && !prevTouch) {
    frozen = !frozen;
    // Force a full header redraw to show FROZEN label immediately
    drawHeader(0);
  }
  prevTouch = touched;

  // Kick off flash on beat
  if (beatNow) {
    flashState = 1;
    flashMs    = millis();
    heartBig   = true;
  }

  // Advance flash frames
  if (flashState > 0 && millis() - flashMs > FLASH_FRAME_MS) {
    flashState++;
    flashMs = millis();
    if (flashState > 3) {
      flashState = 0;
      heartBig   = false;
    }
  }

  // Live dot blink
  if (millis() - liveMs > LIVE_PERIOD_MS) {
    liveMs = millis();
    liveOn = !liveOn;
  }

  // Draw at capped framerate
  if (millis() - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = millis();

    // Choose flash bg colour
    uint16_t fbg = 0;
    if (flashState == 1) fbg = C_FLASH_HI;
    else if (flashState == 2) fbg = C_FLASH_MID;
    else if (flashState == 3) fbg = C_FLASH_LO;

    drawHeader(fbg);
    drawWave();
  }
}
