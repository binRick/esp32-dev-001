#include <TFT_eSPI.h>
#include <WiFi.h>

TFT_eSPI tft = TFT_eSPI();

#define BG        0x0841  // near-black blue
#define HDR_BG    0x0A28  // deep navy
#define HDR_ACCENT 0x055F // teal stripe
#define ROW_A     0x0C63  // row alt A
#define ROW_B     0x0000  // row alt B (pure black)
#define COL_WHITE  TFT_WHITE
#define COL_GRAY   0x7BEF
#define COL_GREEN  0x07E0
#define COL_YELLOW 0xFFE0
#define COL_RED    0xF800
#define COL_TEAL   0x07FF
#define COL_LOCK   0xFD20  // orange

#define SCREEN_W  240
#define SCREEN_H  320
#define HDR_H      48
#define ROW_H      38
#define MAX_ROWS   7   // (320-48)/38 = 7.157

#define SCAN_INTERVAL_MS 6000

int netCount = 0;
int netIndices[32];
unsigned long lastScan = 0;
unsigned long nextScan = 0;

// ── signal bars ───────────────────────────────────────────────────────────────
void drawBars(int x, int y, int rssi) {
  uint16_t color = (rssi >= -60) ? COL_GREEN : (rssi >= -75) ? COL_YELLOW : COL_RED;
  int bars = (rssi >= -50) ? 5 : (rssi >= -60) ? 4 : (rssi >= -70) ? 3 : (rssi >= -80) ? 2 : 1;
  for (int i = 0; i < 5; i++) {
    int bh = 5 + i * 4;
    int bx = x + i * 7;
    int by = y + (20 - bh);
    if (i < bars) tft.fillRect(bx, by, 5, bh, color);
    else          tft.drawRect(bx, by, 5, bh, COL_GRAY);
  }
}

// ── lock icon ─────────────────────────────────────────────────────────────────
void drawLock(int x, int y) {
  tft.fillRoundRect(x, y + 5, 10, 8, 2, COL_LOCK);
  tft.drawRoundRect(x + 2, y + 1, 6, 6, 3, COL_LOCK);
  tft.fillRect(x + 2, y + 4, 6, 4, COL_LOCK);  // fill shackle bottom
  tft.fillRect(x + 4, y + 7, 2, 3, 0x0000);    // keyhole
}

// ── header ────────────────────────────────────────────────────────────────────
void drawHeader(int remaining_ms) {
  tft.fillRect(0, 0, SCREEN_W, HDR_H, HDR_BG);
  tft.fillRect(0, HDR_H - 3, SCREEN_W, 3, HDR_ACCENT);

  // title
  tft.setTextFont(4);
  tft.setTextColor(COL_WHITE, HDR_BG);
  tft.setCursor(10, 10);
  tft.print("WiFi Scanner");

  // network count badge
  tft.setTextFont(2);
  tft.setTextColor(COL_TEAL, HDR_BG);
  tft.setCursor(10, 32);
  if (netCount < 0) {
    tft.print("scanning...");
  } else {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d network%s found", netCount, netCount == 1 ? "" : "s");
    tft.print(buf);
  }

  // countdown
  tft.setTextColor(COL_GRAY, HDR_BG);
  tft.setCursor(SCREEN_W - 52, 32);
  if (remaining_ms > 0) {
    char cbuf[10];
    snprintf(cbuf, sizeof(cbuf), "<%ds", (remaining_ms / 1000) + 1);
    tft.print(cbuf);
  }
}

// ── single network row ────────────────────────────────────────────────────────
void drawRow(int row, int idx) {
  int y = HDR_H + row * ROW_H;
  uint16_t bg = (row % 2 == 0) ? ROW_A : ROW_B;
  tft.fillRect(0, y, SCREEN_W, ROW_H, bg);
  tft.drawFastHLine(0, y + ROW_H - 1, SCREEN_W, HDR_BG);

  String ssid = WiFi.SSID(idx);
  int rssi     = WiFi.RSSI(idx);
  bool locked  = (WiFi.encryptionType(idx) != WIFI_AUTH_OPEN);
  int ch       = WiFi.channel(idx);

  if (ssid.length() == 0) ssid = "(hidden)";
  if (ssid.length() > 15) { ssid = ssid.substring(0, 14); ssid += "~"; }

  // SSID
  tft.setTextFont(2);
  tft.setTextColor(COL_WHITE, bg);
  tft.setCursor(10, y + 5);
  tft.print(ssid);

  // RSSI + channel
  uint16_t rssiCol = (rssi >= -60) ? COL_GREEN : (rssi >= -75) ? COL_YELLOW : COL_RED;
  tft.setTextColor(rssiCol, bg);
  tft.setCursor(10, y + 21);
  char sub[24];
  snprintf(sub, sizeof(sub), "%d dBm  ch%d", rssi, ch);
  tft.print(sub);

  // lock
  if (locked) drawLock(142, y + 8);

  // bars (right side)
  drawBars(170, y + 8, rssi);
}

// ── full scan + redraw ────────────────────────────────────────────────────────
void doScan() {
  netCount = -1;
  drawHeader(0);

  // "scanning" overlay
  tft.fillRect(0, HDR_H, SCREEN_W, SCREEN_H - HDR_H, BG);
  tft.setTextFont(2);
  tft.setTextColor(COL_GRAY, BG);
  tft.setCursor(76, SCREEN_H / 2 - 6);
  tft.print("Scanning...");

  int n = WiFi.scanNetworks();
  netCount = (n < 0) ? 0 : n;

  // sort indices by RSSI descending
  for (int i = 0; i < netCount; i++) netIndices[i] = i;
  for (int i = 0; i < netCount - 1; i++)
    for (int j = 0; j < netCount - i - 1; j++)
      if (WiFi.RSSI(netIndices[j]) < WiFi.RSSI(netIndices[j + 1])) {
        int t = netIndices[j]; netIndices[j] = netIndices[j + 1]; netIndices[j + 1] = t;
      }

  tft.fillRect(0, HDR_H, SCREEN_W, SCREEN_H - HDR_H, BG);

  if (netCount == 0) {
    tft.setTextFont(2);
    tft.setTextColor(COL_GRAY, BG);
    tft.setCursor(58, SCREEN_H / 2 - 6);
    tft.print("No networks found");
  } else {
    int shown = min(netCount, MAX_ROWS);
    for (int r = 0; r < shown; r++) drawRow(r, netIndices[r]);

    if (netCount > MAX_ROWS) {
      int y = HDR_H + MAX_ROWS * ROW_H + 4;
      tft.setTextFont(1);
      tft.setTextColor(COL_GRAY, BG);
      tft.setCursor(88, y);
      char buf[16];
      snprintf(buf, sizeof(buf), "+ %d more", netCount - MAX_ROWS);
      tft.print(buf);
    }
  }

  nextScan = millis() + SCAN_INTERVAL_MS;
  drawHeader(SCAN_INTERVAL_MS);
}

// ── setup / loop ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(BG);
  tft.setTextWrap(false);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  doScan();
}

void loop() {
  unsigned long now = millis();
  if (now >= nextScan) {
    doScan();
  } else {
    // update countdown every second
    static unsigned long lastTick = 0;
    if (now - lastTick >= 1000) {
      lastTick = now;
      long rem = (long)(nextScan - now);
      drawHeader(rem > 0 ? (int)rem : 0);
    }
  }
}
