#include <Arduino.h>
#line 1 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
// dvd_screensaver — Freenove ESP32-S3 Dev Kit (FNK0086)
//
// Classic bouncing DVD logo on the 2.8" ST7789 LCD.
// Logo changes colour each time it hits a wall.
//
// Pin summary (TFT_eSPI / HSPI):
//   MOSI=20  SCLK=21  DC=0  CS=GND

#include <TFT_eSPI.h>

TFT_eSPI tft;

// ── Screen / logo dimensions ──────────────────────────────────────────────────
#define SCREEN_W  240
#define SCREEN_H  320
#define LOGO_W     88   // rounded-rect width
#define LOGO_H     44   // rounded-rect height
#define LOGO_R      8   // corner radius

// ── Colour palette ────────────────────────────────────────────────────────────
const uint16_t PALETTE[] = {
  0xF800,   // red
  0x07E0,   // green
  0x001F,   // blue
  0xFFE0,   // yellow
  0x07FF,   // cyan
  0xF81F,   // magenta
  0xFD20,   // orange
  0xFFFF,   // white
};
const int PALETTE_SIZE = 8;

// ── State ─────────────────────────────────────────────────────────────────────
float    px, py;          // logo top-left (float for smooth sub-pixel motion)
float    vx = 1.7f;
float    vy = 1.3f;
int      ci = 0;          // current palette index
int      px_prev, py_prev;
int      ci_prev;

// ── Draw / erase helpers ──────────────────────────────────────────────────────
#line 42 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
void drawLogo(int x, int y, uint16_t fill);
#line 56 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
void eraseLogo(int x, int y);
#line 61 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
void setup();
#line 78 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
void loop();
#line 42 "/Users/richardblundell/Desktop/repos/esp32-dev-001/sketches/dvd_screensaver/dvd_screensaver.ino"
void drawLogo(int x, int y, uint16_t fill) {
  tft.fillRoundRect(x, y, LOGO_W, LOGO_H, LOGO_R, fill);

  // Thin inner border for depth
  tft.drawRoundRect(x + 2, y + 2, LOGO_W - 4, LOGO_H - 4, LOGO_R - 2,
                    tft.alphaBlend(80, TFT_WHITE, fill));

  // "DVD" text centred, black on the colour fill
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("DVD", x + LOGO_W / 2, y + LOGO_H / 2);
}

void eraseLogo(int x, int y) {
  tft.fillRoundRect(x, y, LOGO_W, LOGO_H, LOGO_R, TFT_BLACK);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Start near centre
  px = (SCREEN_W - LOGO_W) / 2.0f;
  py = (SCREEN_H - LOGO_H) / 2.0f;

  px_prev = (int)px;
  py_prev = (int)py;
  ci_prev = ci;

  drawLogo(px_prev, py_prev, PALETTE[ci]);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  px += vx;
  py += vy;

  bool bounced = false;

  if (px <= 0.0f) {
    px = 0.0f;
    vx =  fabsf(vx);
    bounced = true;
  } else if (px + LOGO_W >= SCREEN_W) {
    px = SCREEN_W - LOGO_W;
    vx = -fabsf(vx);
    bounced = true;
  }

  if (py <= 0.0f) {
    py = 0.0f;
    vy =  fabsf(vy);
    bounced = true;
  } else if (py + LOGO_H >= SCREEN_H) {
    py = SCREEN_H - LOGO_H;
    vy = -fabsf(vy);
    bounced = true;
  }

  if (bounced) {
    // Pick a different colour than the current one
    int next = (ci + 1 + (rand() % (PALETTE_SIZE - 1))) % PALETTE_SIZE;
    ci = next;
  }

  int nx = (int)px;
  int ny = (int)py;

  // Only redraw when the pixel position actually changed
  if (nx != px_prev || ny != py_prev || ci != ci_prev) {
    eraseLogo(px_prev, py_prev);
    drawLogo(nx, ny, PALETTE[ci]);
    px_prev = nx;
    py_prev = ny;
    ci_prev = ci;
  }

  delay(12);   // ~83 fps cap — smooth on ST7789
}

