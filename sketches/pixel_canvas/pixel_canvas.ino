// pixel_canvas — Freenove ESP32-S3 Dev Kit (FNK0086)
//
// Finger-draw pixel art on a 30×30 grid, pick colors from a palette,
// clear, and save each painting to SD as a 24-bit BMP.
//
// Pin summary:
//   LCD   : TFT_eSPI  MOSI=20 SCLK=21 DC=0  CS=GND (ST7789 240×320)
//   Touch : FT6336U   SDA=2   SCL=1
//   SD    : SDMMC     CMD=38  CLK=39  D0=40

#include <TFT_eSPI.h>
#include "FT6336U.h"
#include "SD_MMC.h"
#include <Wire.h>

#define TOUCH_SDA 2
#define TOUCH_SCL 1
#define SD_CLK    39
#define SD_CMD    38
#define SD_D0     40

#define SW 240
#define SH 320

// ── Canvas ────────────────────────────────────────────────────────────────────
#define GRID_W   30
#define GRID_H   30
#define PIXEL_SZ  8   // 30 × 8 = 240 — fills screen width exactly

// ── Toolbar (bottom 80 px, y = 240..319) ─────────────────────────────────────
#define TB_Y  240
#define TB_H   80

// Palette row
#define PAL_Y  (TB_Y + 4)
#define PAL_H   28
#define PAL_N   12
#define PAL_W  (SW / PAL_N)   // 20 px per swatch

// Button row
#define BTN_Y        (PAL_Y + PAL_H + 4)
#define BTN_H         36
#define BTN_ERASE_X    0
#define BTN_ERASE_W   74
#define BTN_CLEAR_X   79
#define BTN_CLEAR_W   79
#define BTN_SAVE_X   162
#define BTN_SAVE_W    78

// ── Palette colours (RGB565) ──────────────────────────────────────────────────
#define PAL_BLACK   0x0000
#define PAL_WHITE   0xFFFF
#define PAL_RED     0xF800
#define PAL_ORANGE  0xFD20
#define PAL_YELLOW  0xFFE0
#define PAL_LIME    0x07E0
#define PAL_DKGREEN 0x0400
#define PAL_CYAN    0x07FF
#define PAL_BLUE    0x001F
#define PAL_PURPLE  0x8010
#define PAL_MAGENTA 0xF81F
#define PAL_GREY    0xA514

static const uint16_t PALETTE[PAL_N] = {
  PAL_BLACK, PAL_WHITE,   PAL_RED,    PAL_ORANGE,
  PAL_YELLOW, PAL_LIME,   PAL_DKGREEN, PAL_CYAN,
  PAL_BLUE,  PAL_PURPLE,  PAL_MAGENTA, PAL_GREY,
};

// ── UI colours ────────────────────────────────────────────────────────────────
#define C_BG       0x0000
#define C_TB_BG    0x2104
#define C_DIVIDER  0x4208
#define C_SEL_RING 0xFFFF
#define C_BTN_BG   0x4A49
#define C_BTN_TXT  0xFFFF

// ── BMP row stride (30 × 3 bytes = 90, padded to 92) ─────────────────────────
static const int BMP_ROW_BYTES = ((GRID_W * 3 + 3) / 4) * 4;

// ── State ─────────────────────────────────────────────────────────────────────
TFT_eSPI tft;
FT6336U  ts(TOUCH_SDA, TOUCH_SCL, -1, -1);

uint16_t canvas[GRID_H][GRID_W];
int      selectedColor        = 0;
bool     eraserActive         = false;
bool     sdOK                 = false;
bool     prevTouched          = false;
bool     touchStartedInCanvas = false;

// ── Drawing helpers ───────────────────────────────────────────────────────────
void drawCanvasPixel(int col, int row, uint16_t color) {
  tft.fillRect(col * PIXEL_SZ, row * PIXEL_SZ, PIXEL_SZ, PIXEL_SZ, color);
}

void clearCanvas() {
  memset(canvas, 0, sizeof(canvas));
  tft.fillRect(0, 0, SW, GRID_H * PIXEL_SZ, C_BG);
}

void drawPaletteRow() {
  for (int i = 0; i < PAL_N; i++) {
    int x = i * PAL_W;
    uint16_t border = (i == selectedColor && !eraserActive) ? C_SEL_RING : C_TB_BG;
    tft.drawRect(x, PAL_Y, PAL_W, PAL_H, border);
    tft.fillRect(x + 1, PAL_Y + 1, PAL_W - 2, PAL_H - 2, PALETTE[i]);
  }
}

void drawButton(int x, int w, const char* label, bool active) {
  uint16_t bg = active ? PAL_ORANGE : C_BTN_BG;
  tft.fillRoundRect(x, BTN_Y, w, BTN_H, 4, bg);
  tft.setTextColor(C_BTN_TXT, bg);
  tft.setTextFont(2);
  tft.setTextSize(1);
  int tw = tft.textWidth(label);
  tft.setCursor(x + (w - tw) / 2, BTN_Y + (BTN_H - 14) / 2);
  tft.print(label);
}

void drawToolbar() {
  tft.fillRect(0, TB_Y, SW, TB_H, C_TB_BG);
  tft.drawFastHLine(0, TB_Y, SW, C_DIVIDER);
  drawPaletteRow();
  drawButton(BTN_ERASE_X, BTN_ERASE_W, "ERASE", eraserActive);
  drawButton(BTN_CLEAR_X, BTN_CLEAR_W, "CLEAR", false);
  drawButton(BTN_SAVE_X,  BTN_SAVE_W,  sdOK ? "SAVE" : "NO SD", false);
}

// ── BMP save ──────────────────────────────────────────────────────────────────
static void w16(File& f, uint16_t v) {
  f.write(v & 0xFF); f.write(v >> 8);
}
static void w32(File& f, uint32_t v) {
  f.write(v & 0xFF); f.write((v >> 8) & 0xFF);
  f.write((v >> 16) & 0xFF); f.write((v >> 24) & 0xFF);
}

static int findNextFileNum() {
  for (int i = 1; i <= 999; i++) {
    char path[32];
    snprintf(path, sizeof(path), "/canvas_%03d.bmp", i);
    if (!SD_MMC.exists(path)) return i;
  }
  return 1;
}

bool saveCanvas() {
  if (!sdOK) return false;

  char path[32];
  snprintf(path, sizeof(path), "/canvas_%03d.bmp", findNextFileNum());
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;

  uint32_t pixBytes = (uint32_t)BMP_ROW_BYTES * GRID_H;
  uint32_t fileSize = 54 + pixBytes;

  // BMP file header
  f.write('B'); f.write('M');
  w32(f, fileSize);
  w16(f, 0); w16(f, 0);   // reserved
  w32(f, 54);              // pixel data offset

  // BITMAPINFOHEADER
  w32(f, 40);
  w32(f, GRID_W); w32(f, GRID_H);
  w16(f, 1);               // planes
  w16(f, 24);              // bits per pixel
  w32(f, 0);               // compression
  w32(f, 0);               // image size
  w32(f, 2835); w32(f, 2835);  // pixels/meter (~72 DPI)
  w32(f, 0); w32(f, 0);   // palette

  // Pixel rows — BMP is bottom-up, stored as B G R
  uint8_t rowBuf[BMP_ROW_BYTES];
  for (int gr = GRID_H - 1; gr >= 0; gr--) {
    memset(rowBuf, 0, BMP_ROW_BYTES);
    for (int gc = 0; gc < GRID_W; gc++) {
      uint16_t px = canvas[gr][gc];
      uint8_t r5 = (px >> 11) & 0x1F;
      uint8_t g6 = (px >> 5)  & 0x3F;
      uint8_t b5 =  px        & 0x1F;
      rowBuf[gc * 3 + 0] = (b5 << 3) | (b5 >> 2);
      rowBuf[gc * 3 + 1] = (g6 << 2) | (g6 >> 4);
      rowBuf[gc * 3 + 2] = (r5 << 3) | (r5 >> 2);
    }
    f.write(rowBuf, BMP_ROW_BYTES);
  }
  f.close();

  Serial.printf("Saved: %s\n", path);
  return true;
}

// ── Save feedback overlay ─────────────────────────────────────────────────────
void showSavedFeedback(bool success) {
  const char* msg = success ? "SAVED!" : "SAVE FAILED";
  uint16_t bg = success ? 0x07E0 : 0xF800;
  const int bw = 130, bh = 40;
  int bx = (SW - bw) / 2;
  int by = (GRID_H * PIXEL_SZ - bh) / 2;
  tft.fillRoundRect(bx, by, bw, bh, 6, bg);
  tft.setTextColor(TFT_BLACK, bg);
  tft.setTextFont(4);
  tft.setTextSize(1);
  int tw = tft.textWidth(msg);
  tft.setCursor(bx + (bw - tw) / 2, by + (bh - 26) / 2);
  tft.print(msg);
  delay(800);
  // Restore covered canvas pixels
  int r0 = max(0, by / PIXEL_SZ);
  int r1 = min(GRID_H - 1, (by + bh) / PIXEL_SZ + 1);
  int c0 = max(0, bx / PIXEL_SZ);
  int c1 = min(GRID_W - 1, (bx + bw) / PIXEL_SZ + 1);
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++)
      drawCanvasPixel(c, r, canvas[r][c]);
}

// ── Toolbar touch handler ─────────────────────────────────────────────────────
void handleToolbarTouch(int tx, int ty) {
  if (ty >= PAL_Y && ty < PAL_Y + PAL_H) {
    int idx = tx / PAL_W;
    if (idx >= 0 && idx < PAL_N) {
      selectedColor = idx;
      eraserActive  = false;
      drawPaletteRow();
      drawButton(BTN_ERASE_X, BTN_ERASE_W, "ERASE", false);
    }
  } else if (ty >= BTN_Y && ty < BTN_Y + BTN_H) {
    if (tx >= BTN_ERASE_X && tx < BTN_ERASE_X + BTN_ERASE_W) {
      eraserActive = !eraserActive;
      drawPaletteRow();
      drawButton(BTN_ERASE_X, BTN_ERASE_W, "ERASE", eraserActive);
    } else if (tx >= BTN_CLEAR_X && tx < BTN_CLEAR_X + BTN_CLEAR_W) {
      clearCanvas();
    } else if (tx >= BTN_SAVE_X && tx < BTN_SAVE_X + BTN_SAVE_W) {
      showSavedFeedback(saveCanvas());
    }
  }
}

// ── Setup & loop ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  ts.begin();

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  sdOK = SD_MMC.begin("/sdcard", true);
  if (!sdOK) Serial.println("SD not mounted — saves disabled");

  memset(canvas, 0, sizeof(canvas));
  drawToolbar();
}

void loop() {
  FT6336U_TouchPointType tp = ts.scan();
  bool touched    = (tp.touch_count > 0);
  int  tx         = touched ? (int)tp.tp[0].x : -1;
  int  ty         = touched ? (int)tp.tp[0].y : -1;
  bool freshTouch = touched && !prevTouched;

  if (freshTouch) {
    touchStartedInCanvas = (ty < TB_Y);
  }

  if (touched) {
    if (touchStartedInCanvas && ty >= 0 && ty < GRID_H * PIXEL_SZ) {
      int col = tx / PIXEL_SZ;
      int row = ty / PIXEL_SZ;
      if (col >= 0 && col < GRID_W && row >= 0 && row < GRID_H) {
        uint16_t color = eraserActive ? C_BG : PALETTE[selectedColor];
        if (canvas[row][col] != color) {
          canvas[row][col] = color;
          drawCanvasPixel(col, row, color);
        }
      }
    } else if (freshTouch && !touchStartedInCanvas && ty >= TB_Y) {
      handleToolbarTouch(tx, ty);
    }
  }

  prevTouched = touched;
  delay(8);
}
