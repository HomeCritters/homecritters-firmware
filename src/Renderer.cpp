#include "Renderer.h"
#include <cstring>
#include <Preferences.h>
#include <qrcode.h>
#include "GameConfig.h"
#include "ferret_game.h"  // small ferret sprite for the mini-game (only here)

using namespace theme;
using namespace ui;

// Forest ground line (where the grass meets the sky / treeline).
static constexpr int GROUND_Y = 108;

// Lerp two RGB565 colors (t in 0..1). Used for the sky gradient.
static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void Renderer::begin() {
  _lcd.init();
  _lcd.setRotation(0);
  Preferences p;
  p.begin("disp", true);
  _scrBright = p.getInt("scr", 70);
  p.end();
  _lcd.setBrightness(map(_scrBright, 0, 100, 0, 255));
  _canvas.setColorDepth(16);
  _canvas.createSprite(SCREEN_W, SCREEN_H);
  // Ferret frames are big-endian RGB565; without this brown turns green.
  _canvas.setSwapBytes(true);

  // Snapshot buffer for the web screenshot (PSRAM; falls back to heap).
  _snap = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * 2);
  if (!_snap) _snap = (uint16_t*)malloc(SCREEN_W * SCREEN_H * 2);
}

// Copy the finished canvas into the stable snapshot buffer (render thread).
void Renderer::takeWebSnapshot() {
  const void* buf = _canvas.getBuffer();
  if (_snap && buf) memcpy(_snap, buf, SCREEN_W * SCREEN_H * 2);
  _snapReq = false;
  _snapReady = true;
}

// Backlight brightness with a floor so the screen can never be turned fully
// dark (which would make it impossible to turn back up on the device).
void Renderer::setScreenBrightness(int pct) {
  _scrBright = constrain(pct, 20, 100);
  _lcd.setBrightness(map(_scrBright, 0, 100, 0, 255));
  Preferences p;
  p.begin("disp", false);
  p.putInt("scr", _scrBright);
  p.end();
}

void Renderer::flashButton(int idx) {
  _pressedButton = idx;
  _pressedUntil = millis() + game::BUTTON_FLASH_MS;
}

// Dumps the last-rendered canvas (RGB565) over Serial. Framing: a text header
// "@@SHOT <w> <h>\n", then exactly w*h*2 raw little-endian bytes, then "@@END".
// The host reads a fixed byte count, so binary that happens to contain "@@END"
// is harmless.
void Renderer::captureScreenshot() {
  const uint8_t* buf = (const uint8_t*)_canvas.getBuffer();  // raw RGB565, row-major
  Serial.printf("\n@@SHOT %d %d\n", SCREEN_W, SCREEN_H);
  if (buf) {
    // Chunked writes keep the USB-CDC FIFO happy for the full 115KB frame.
    const uint32_t total = (uint32_t)SCREEN_W * SCREEN_H * 2;
    for (uint32_t off = 0; off < total; off += 1024) {
      const uint32_t n = (total - off < 1024) ? (total - off) : 1024;
      Serial.write(buf + off, n);
      Serial.flush();
    }
  }
  Serial.print("\n@@END\n");
}

void Renderer::drawSky() {
  // Vertical gradient from the top of the sky down to the ground line.
  for (int y = 0; y < GROUND_Y; y++) {
    float t = (float)y / (float)GROUND_Y;
    _canvas.drawFastHLine(0, y, SCREEN_W, lerp565(_p.skyTop, _p.skyBottom, t));
  }
}

void Renderer::drawStars() {
  static const int16_t stars[][2] = {
    {20, 18}, {40, 60}, {70, 30}, {110, 20}, {58, 90},
    {150, 55}, {205, 70}, {225, 30}, {18, 80}, {130, 78},
  };
  for (auto& s : stars) _canvas.fillCircle(s[0], s[1], 1, STAR);
  _canvas.fillCircle(90, 45, 1, STAR);
  _canvas.fillCircle(178, 25, 1, STAR);
}

void Renderer::drawMoon() {
  const int mx = 186, my = 40;
  _canvas.fillCircle(mx, my, 20, MOON_GLOW);  // soft halo
  _canvas.fillCircle(mx, my, 15, MOON);
  _canvas.fillCircle(mx - 5, my - 3, 3, MOON_CRATER);  // craters
  _canvas.fillCircle(mx + 4, my + 4, 2, MOON_CRATER);
  _canvas.fillCircle(mx + 2, my - 6, 2, MOON_CRATER);
}

void Renderer::drawSun() {
  const int sx = 186, sy = 40;
  _canvas.fillCircle(sx, sy, 19, SUN_GLOW);  // glow
  _canvas.fillCircle(sx, sy, 14, SUN);
  // short rays
  for (int a = 0; a < 360; a += 45) {
    float r = a * 3.14159f / 180.0f;
    int x0 = sx + (int)(cosf(r) * 17), y0 = sy + (int)(sinf(r) * 17);
    int x1 = sx + (int)(cosf(r) * 23), y1 = sy + (int)(sinf(r) * 23);
    _canvas.drawLine(x0, y0, x1, y1, SUN);
  }
}

void Renderer::drawSunset() {
  // Big low sun near the horizon, on the right, with a warm halo.
  const int sx = 180, sy = 86;
  _canvas.fillCircle(sx, sy, 24, SUNSET_GLOW);
  _canvas.fillCircle(sx, sy, 17, SUNSET);
}

// A simple pine tree: trunk + stacked triangular foliage layers.
void Renderer::drawPineTree(int bx, int baseY, int size) {
  _canvas.fillRect(bx - 1, baseY - size / 5, 3, size / 5, _p.treeTrunk);
  int layers = 3;
  int top = baseY - size / 5;
  for (int i = 0; i < layers; i++) {
    int w = (size / 2) * (layers - i) / layers + 3;
    int y0 = top - (size * i) / (layers + 1);
    int y1 = y0 - (size * 2) / (layers + 1);
    _canvas.fillTriangle(bx - w, y0, bx + w, y0, bx, y1, _p.treeNear);
  }
}

void Renderer::drawCabin(int bx, int by, bool night) {
  const int w = 44, wallH = 22;
  // wall
  _canvas.fillRect(bx, by - wallH, w, wallH, _p.cabinWall);
  // log lines (stacked timbers)
  for (int y = by - wallH + 4; y < by; y += 5) {
    _canvas.drawFastHLine(bx, y, w, _p.cabinRoof);
  }
  // roof with eaves
  _canvas.fillTriangle(bx - 5, by - wallH, bx + w + 5, by - wallH,
                       bx + w / 2, by - wallH - 15, _p.cabinRoof);
  // door
  _canvas.fillRect(bx + 5, by - 14, 10, 14, _p.cabinRoof);
  // window (lit at night, with a glow)
  int wx = bx + w - 16, wy = by - 16;
  if (night) _canvas.fillCircle(wx + 4, wy + 4, 8, lerp565(_p.cabinWindow, _p.skyTop, 0.55f));
  _canvas.fillRect(wx, wy, 9, 9, _p.cabinWindow);
  _canvas.drawFastVLine(wx + 4, wy, 9, _p.cabinRoof);
  _canvas.drawFastHLine(wx, wy + 4, 9, _p.cabinRoof);
}

void Renderer::drawForest(bool night) {
  // distant treeline (silhouette) along the horizon
  for (int x = -6; x < SCREEN_W + 6; x += 16) {
    int h = 26 + ((x * 7) % 13);
    _canvas.fillTriangle(x - 10, GROUND_Y, x + 10, GROUND_Y, x, GROUND_Y - h, _p.treeFar);
  }

  // ground (grass)
  _canvas.fillRect(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y, _p.ground);

  // cabin in the back (left) + pines around it for depth
  drawCabin(30, GROUND_Y, night);
  drawPineTree(90, GROUND_Y, 34);
  drawPineTree(210, GROUND_Y, 40);
  drawPineTree(178, GROUND_Y, 28);

  // darker grass tufts up front
  for (int x = 4; x < SCREEN_W; x += 18) {
    int gy = GROUND_Y + 6 + ((x * 5) % 7);
    _canvas.fillTriangle(x - 3, gy, x + 3, gy, x, gy - 6, _p.groundDark);
  }
}

void Renderer::drawSparkles(bool night) {
  // Magic dust / fireflies: tiny dots that slowly twinkle.
  static const int16_t pts[][2] = {
    {60, 70}, {150, 58}, {110, 90}, {30, 95}, {200, 88},
    {170, 120}, {75, 130}, {130, 140}, {215, 130}, {45, 128},
  };
  unsigned long ph = millis() / 250;
  for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
    // light only part of them at a time (twinkle)
    if (((ph + i) % 3) == 0) continue;
    if (!night && i % 2) continue;  // fewer sparkles by day
    _canvas.fillCircle(pts[i][0], pts[i][1], 1, _p.sparkle);
  }
}

void Renderer::drawHeader(const Pet& pet, bool wifiOn) {
  const Mood mood = pet.mood();
  _canvas.setTextColor(_p.text);
  _canvas.setTextSize(2);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(pet.name().c_str()) / 2, 20);
  _canvas.print(pet.name());

  // WiFi indicator (small dot), inside the visible circle next to the status
  if (wifiOn) _canvas.fillCircle(196, 42, 3, _p.sparkle);

  // "Dormindo" only when actually asleep; MOOD_SLEEPY while awake means tired.
  const char* status = "Feliz";
  if (pet.sleeping()) {
    status = "Dormindo";
  } else {
    switch (mood) {
      case MOOD_HAPPY:   status = "Feliz";            break;
      case MOOD_NEUTRAL: status = "Tranquilo";        break;
      case MOOD_SAD:     status = "Triste";           break;
      case MOOD_HUNGRY:  status = "Com fome";         break;
      case MOOD_SLEEPY:  status = "Com sono";         break;
      case MOOD_DIRTY:   status = "Precisa de banho"; break;
    }
  }
  _canvas.setTextSize(1);
  _canvas.setTextColor(_p.textDim);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(status) / 2, 40);
  _canvas.print(status);
}

void Renderer::drawClock(Clock& clock) {
  char t[8], d[16];
  clock.format(t, sizeof(t), d, sizeof(d));
  // clock panel replaces the HUD (the ferret keeps wandering above)
  _canvas.fillRoundRect(34, 140, 172, 60, 12, menu::CLOCK_BG);
  _canvas.drawRoundRect(34, 140, 172, 60, 12, menu::CLOCK_EDGE);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(4);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(t) / 2, 150);
  _canvas.print(t);
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::CLOCK_DATE);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(d) / 2, 186);
  _canvas.print(d);
}

void Renderer::drawMenuHandle() {
  // Small "v" tab at the top, hinting at pull-down (config menu).
  _canvas.fillRoundRect(HANDLE_CX - HANDLE_W / 2, -6, HANDLE_W, HANDLE_H + 4, 6, BTN_BG);
  _canvas.drawRoundRect(HANDLE_CX - HANDLE_W / 2, -6, HANDLE_W, HANDLE_H + 4, 6, BTN_BORDER);
  _canvas.fillTriangle(HANDLE_CX - 7, 4, HANDLE_CX + 7, 4, HANDLE_CX, 11, BTN_BORDER);
}

void Renderer::drawRightHandle() {
  // Tab on the right edge with a "<" chevron, hinting at pull-left (games).
  const int rx = SCREEN_W - RHANDLE_W, ry = RHANDLE_CY - RHANDLE_H / 2;
  _canvas.fillRoundRect(rx, ry, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BG);
  _canvas.drawRoundRect(rx, ry, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BORDER);
  _canvas.fillTriangle(rx + 9, RHANDLE_CY - 7, rx + 9, RHANDLE_CY + 7, rx + 2, RHANDLE_CY, BTN_BORDER);
}

// Draws a QR code (version 3, byte mode) centered horizontally at cx.
void Renderer::drawQr(const char* text, int topY, int cx, int quiet) {
  QRCode qr;
  static uint8_t buf[200];  // version 3 needs 107 bytes
  qrcode_initText(&qr, buf, 3, ECC_LOW, text);

  const int scale = 2;
  const int total = (qr.size + quiet * 2) * scale;
  const int ox = cx - total / 2;
  // white background (incl. quiet zone)
  _canvas.fillRect(ox, topY, total, total, TFT_WHITE);
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        _canvas.fillRect(ox + (x + quiet) * scale, topY + (y + quiet) * scale,
                         scale, scale, TFT_BLACK);
      }
    }
  }
}

// Filled rounded button with a centered label (text size 2).
void Renderer::drawPillButton(int x, int y, int w, int h, const char* label, uint16_t bg) {
  _canvas.fillRoundRect(x, y, w, h, 8, bg);
  _canvas.drawRoundRect(x, y, w, h, 8, BTN_BORDER);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(x + w / 2 - _canvas.textWidth(label) / 2, y + h / 2 - 7);
  _canvas.print(label);
}

// A labeled +/- stepper with a fill track (shared by volume/LED/screen).
void Renderer::drawStepper(const char* label, int pct, const ui::ButtonSlot& minus,
                           const ui::ButtonSlot& plus) {
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  char t[24];
  snprintf(t, sizeof(t), "%s  %d%%", label, pct);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(t) / 2, minus.cy - 22);
  _canvas.print(t);

  _canvas.fillCircle(minus.cx, minus.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(minus.cx - 8, minus.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillCircle(plus.cx, plus.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(plus.cx - 8, plus.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillRect(plus.cx - 2, plus.cy - 8, 4, 16, TFT_WHITE);

  const int bx = minus.cx + MENU_BTN_R + 8;
  const int bw = (plus.cx - MENU_BTN_R - 8) - bx;
  _canvas.fillRoundRect(bx, minus.cy - 6, bw, 12, 4, menu::VOL_TRACK);
  _canvas.fillRoundRect(bx, minus.cy - 6, bw * pct / 100, 12, 4, BAR_HIGH);
}

// A speaker box (cabinet) with a tweeter + woofer, centered at (cx,cy). The
// driver centers are punched out in the tile bg so they read as cones.
void Renderer::drawAudioIcon(int cx, int cy, uint16_t bg) {
  _canvas.fillRoundRect(cx - 10, cy - 14, 20, 28, 4, menu::IC_AUDIO);   // cabinet
  _canvas.drawRoundRect(cx - 10, cy - 14, 20, 28, 4, rgb565(35, 80, 160));
  _canvas.fillCircle(cx, cy - 7, 3, menu::IC_AUDIO2);                   // tweeter
  _canvas.fillCircle(cx, cy - 7, 1, bg);
  _canvas.fillCircle(cx, cy + 5, 6, menu::IC_AUDIO2);                   // woofer
  _canvas.fillCircle(cx, cy + 5, 2, bg);
}

// A light bulb: amber glass with a highlight + a gray screw base, at (cx,cy).
void Renderer::drawLightIcon(int cx, int cy) {
  const uint16_t base = rgb565(150, 150, 160);
  _canvas.fillCircle(cx, cy - 4, 9, menu::IC_SUN);        // glass
  _canvas.fillCircle(cx - 3, cy - 7, 3, menu::IC_SUN2);   // highlight
  _canvas.fillRect(cx - 5, cy + 5, 10, 3, base);          // neck
  _canvas.fillRoundRect(cx - 4, cy + 8, 8, 6, 2, base);   // screw base
  _canvas.drawFastHLine(cx - 4, cy + 10, 8, menu::CELL_LABEL);  // thread lines
  _canvas.drawFastHLine(cx - 4, cy + 12, 8, menu::CELL_LABEL);
}

// Classic WiFi symbol: three radiating arcs above a dot, green. The arcs are
// plotted point-by-point (a ~110-degree fan) so it doesn't depend on any
// arc-drawing API's angle convention.
void Renderer::drawWifiIcon(int cx, int cy) {
  const uint16_t c = menu::IC_WIFI;
  const int dotx = cx, doty = cy + 10;  // emitter dot near the bottom
  const int radii[3] = {7, 12, 17};
  for (int b = 0; b < 3; b++) {
    for (int deg = -55; deg <= 55; deg += 4) {
      const float t = deg * 3.14159f / 180.0f;
      const int x = dotx + (int)(radii[b] * sinf(t));
      const int y = doty - (int)(radii[b] * cosf(t));
      _canvas.fillCircle(x, y, 1, c);  // ~3px-thick arc
    }
  }
  _canvas.fillCircle(dotx, doty, 2, c);  // emitter dot
}

// One grid cell: light tile, colored icon (a/l/w) on top, label under it.
void Renderer::drawGridCell(int x, int y, const char* label, char icon) {
  _canvas.fillRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, BTN_BORDER);
  const int cx = x + MENU_CELL_W / 2, iy = y + 26;
  switch (icon) {
    case 'a': drawAudioIcon(cx, iy, menu::CELL_BG); break;
    case 'l': drawLightIcon(cx, iy);                break;
    case 'w': drawWifiIcon(cx, iy);                 break;
  }
  _canvas.setTextColor(menu::CELL_LABEL);
  _canvas.setTextSize(1);
  _canvas.setCursor(cx - _canvas.textWidth(label) / 2, y + MENU_CELL_H - 15);
  _canvas.print(label);
}

void Renderer::drawMenu(ui::MenuPage page, int volume, int ledBright,
                        int batteryPct, bool wifiOn, const char* ip) {
  _canvas.fillScreen(menu::BG);  // full-screen dark purple background
  if (page == PAGE_AUDIO)      drawMenuAudio(volume);
  else if (page == PAGE_LIGHT) drawMenuLight(ledBright);
  else if (page == PAGE_QR)    drawMenuQr(wifiOn, ip);
  else                         drawMenuMain(batteryPct, wifiOn, ip);
}

// Main page: title + battery, a 2x2 grid - Audio/Luz (top), WiFi/QR (bottom).
// The QR tile shows the code and is tappable (opens the QR detail page). + Voltar.
void Renderer::drawMenuMain(int batteryPct, bool wifiOn, const char* ip) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Config";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 6);
  _canvas.print(title);
  drawBatteryPill(26, batteryPct, TFT_WHITE, menu::TEXT_DIM);

  drawGridCell(MENU_COL_L, MENU_ROW_1, "Audio", 'a');
  drawGridCell(MENU_COL_R, MENU_ROW_1, "Luz", 'l');
  drawGridCell(MENU_COL_L, MENU_ROW_2, "WiFi", 'w');

  // Bottom-right QR tile (white, rounded to match the grid). When offline it
  // shows a placeholder; either way it opens the QR detail page on tap.
  _canvas.fillRoundRect(MENU_COL_R, MENU_ROW_2, MENU_CELL_W, MENU_CELL_H, 12, TFT_WHITE);
  _canvas.drawRoundRect(MENU_COL_R, MENU_ROW_2, MENU_CELL_W, MENU_CELL_H, 12, BTN_BORDER);
  if (wifiOn && ip && ip[0]) {
    char qrUrl[40];
    snprintf(qrUrl, sizeof(qrUrl), "http://%s/", ip);
    drawQr(qrUrl, MENU_ROW_2 + 2, MENU_QR_CX, 1);  // 62px, centered in the 66px tile
  } else {
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CELL_LABEL);
    const char* q = "QR";
    _canvas.setCursor(MENU_QR_CX - _canvas.textWidth(q) / 2, MENU_ROW_2 + MENU_CELL_H / 2 - 4);
    _canvas.print(q);
  }

  drawPillButton(MENU_BACK_X, MENU_BACK_Y, MENU_BACK_W, MENU_BACK_H, "Voltar", menu::CLOSE_BTN);
}

// QR detail page: the code (large) + what it is + how to use it + the URLs.
void Renderer::drawMenuQr(bool wifiOn, const char* ip) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "QR Code";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 8);
  _canvas.print(title);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  if (wifiOn && ip && ip[0]) {
    char qrUrl[40];
    snprintf(qrUrl, sizeof(qrUrl), "http://%s/", ip);
    drawQr(qrUrl, 30);  // centered, ~70px
    const char* l1 = "Aponte a camera do celular";
    const char* l2 = "para abrir o painel de";
    const char* l3 = "controle no navegador.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 108);
    _canvas.print(l1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 118);
    _canvas.print(l2);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l3) / 2, 128);
    _canvas.print(l3);
    char urlIp[40];
    snprintf(urlIp, sizeof(urlIp), "http://%s", ip);
    _canvas.setTextColor(menu::AP_NAME);
    _canvas.setCursor(CENTER_X - _canvas.textWidth("http://ferret.local") / 2, 144);
    _canvas.print("http://ferret.local");
    _canvas.setCursor(CENTER_X - _canvas.textWidth(urlIp) / 2, 154);
    _canvas.print(urlIp);
  } else {
    const char* m1 = "Conecte o WiFi primeiro";
    const char* m2 = "para gerar o QR do painel.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m1) / 2, 96);
    _canvas.print(m1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m2) / 2, 110);
    _canvas.print(m2);
  }

  drawPillButton(MENU_BACK_X, MENU_BACK_Y, MENU_BACK_W, MENU_BACK_H, "Voltar", menu::CLOSE_BTN);
}

void Renderer::drawMenuAudio(int volume) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Audio";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("Volume", volume, MENU_VOL_MINUS, MENU_VOL_PLUS);
  drawPillButton(MENU_BACK_X, MENU_BACK_Y, MENU_BACK_W, MENU_BACK_H, "Voltar", menu::CLOSE_BTN);
}

void Renderer::drawMenuLight(int ledBright) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Luz";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("LED", ledBright, MENU_LED_MINUS, MENU_LED_PLUS);
  drawStepper("Tela", _scrBright, MENU_SCR_MINUS, MENU_SCR_PLUS);
  drawPillButton(MENU_BACK_X, MENU_BACK_Y, MENU_BACK_W, MENU_BACK_H, "Voltar", menu::CLOSE_BTN);
}

void Renderer::drawWifiConfig(const char* apName) {
  _canvas.fillScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* t = "Config WiFi";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(t) / 2, 34);
  _canvas.print(t);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l1 = "No celular, conecte na rede:";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 78);
  _canvas.print(l1);

  _canvas.setTextSize(2);
  _canvas.setTextColor(menu::AP_NAME);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(apName) / 2, 98);
  _canvas.print(apName);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l2 = "e siga o portal que abrir.";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 132);
  _canvas.print(l2);

  drawPillButton(WIFI_EXIT_X, WIFI_EXIT_Y, WIFI_EXIT_W, WIFI_EXIT_H,
                 "Sair", menu::EXIT_BTN);

  _canvas.pushSprite(0, 0);
}

// ------------------- Games -------------------

// A game tile: light square with a colored icon on top and a label under it.
void Renderer::drawGameTile(int x, int y, const char* label, char icon, uint16_t iconColor) {
  _canvas.fillRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, BTN_BORDER);
  const int cx = x + GAME_TILE_W / 2, cy = y + 34;
  if (icon == 'j') {  // Doodle Jump: the doodler bouncing on a platform
    const uint16_t plat = rgb565(45, 140, 70);  // platform (darker green)
    _canvas.fillRoundRect(cx - 16, cy + 13, 32, 6, 3, plat);
    _canvas.fillTriangle(cx - 4, cy - 15, cx + 4, cy - 15, cx, cy - 20, iconColor);  // jump arrow
    _canvas.fillRoundRect(cx - 10, cy - 9, 20, 18, 6, iconColor);   // body
    _canvas.fillRect(cx - 7, cy + 8, 4, 6, iconColor);              // feet
    _canvas.fillRect(cx + 3, cy + 8, 4, 6, iconColor);
    _canvas.fillCircle(cx - 4, cy - 3, 3, TFT_WHITE);               // eyes
    _canvas.fillCircle(cx + 4, cy - 3, 3, TFT_WHITE);
    _canvas.fillCircle(cx - 4, cy - 3, 1, menu::CELL_LABEL);
    _canvas.fillCircle(cx + 4, cy - 3, 1, menu::CELL_LABEL);
  } else {            // Bolinha: a yellow tennis ball with a curved seam
    _canvas.fillCircle(cx, cy, 16, iconColor);
    _canvas.drawCircle(cx, cy, 16, rgb565(150, 175, 40));  // rim
    for (int dy = -14; dy <= 14; dy++) {                   // hourglass seam
      const float f = 1.0f - (float)(dy * dy) / (14.0f * 14.0f);
      const int off = (int)(7 * f);
      _canvas.fillCircle(cx - 11 + off, cy + dy, 1, TFT_WHITE);
      _canvas.fillCircle(cx + 11 - off, cy + dy, 1, TFT_WHITE);
    }
  }
  _canvas.setTextColor(menu::CELL_LABEL);
  _canvas.setTextSize(1);
  _canvas.setCursor(cx - _canvas.textWidth(label) / 2, y + GAME_TILE_H - 15);
  _canvas.print(label);
}

void Renderer::drawGamesMenu() {
  _canvas.fillScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Jogos";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 20);
  _canvas.print(title);

  drawGameTile(GAME_COL_L, GAME_TILE_Y, "Jump!", 'j', menu::IC_DOODLE);
  drawGameTile(GAME_COL_R, GAME_TILE_Y, "Bolinha", 'b', menu::IC_BALL);

  drawPillButton(GAMES_BACK_X, GAMES_BACK_Y, GAMES_BACK_W, GAMES_BACK_H,
                 "Voltar", menu::CLOSE_BTN);
  _canvas.pushSprite(0, 0);
}

// The official ferret sprite (game-sized), jump animation, centered at (cx,cy).
void Renderer::drawDoodleFerret(int cx, int cy, bool faceLeft) {
  const int idx = (millis() / 80) % FERRET_G_JUMP_FRAMES;
  const uint16_t* fr = faceLeft ? &ferret_g_jump_l[idx][0] : &ferret_g_jump[idx][0];
  // Cast the key to uint16_t so the same (transparency-applying) pushImage
  // overload is picked as the scene ferret - a bare 0xF81F macro is an int
  // literal and selects a variant that leaves the magenta background opaque.
  _canvas.pushImage(cx - FERRET_G_FRAME_W / 2, cy - FERRET_G_FRAME_H / 2,
                    FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                    (uint16_t)FERRET_G_TRANSPARENT_KEY);
}

void Renderer::drawDoodle(DoodleGame& game) {
  _canvas.fillScreen(rgb565(150, 205, 235));  // light sky

  // parallax clouds: drift down slower than the platforms as the run climbs
  static const int16_t clouds[3][2] = {{52, 34}, {182, 96}, {96, 168}};
  const int coff = (int)(game.climbPx() * 0.35f);
  const uint16_t cloudCol = rgb565(226, 240, 250);
  for (auto& c : clouds) {
    const int cy = ((c[1] + coff) % 280) - 20;
    _canvas.fillCircle(c[0], cy, 10, cloudCol);
    _canvas.fillCircle(c[0] - 12, cy + 3, 7, cloudCol);
    _canvas.fillCircle(c[0] + 12, cy + 3, 7, cloudCol);
  }

  const auto* plats = game.platforms();
  for (int i = 0; i < DoodleGame::PLAT_COUNT; i++) {
    const auto& p = plats[i];
    if (!p.active) continue;
    const int px = (int)p.x, py = (int)p.y;
    // Color says type: green = solid, blue = sliding, beige+crack = crumble.
    uint16_t fill = rgb565(70, 175, 80), edge = rgb565(45, 120, 55);
    if (p.vx != 0)       { fill = rgb565(70, 150, 200); edge = rgb565(40, 95, 140); }
    else if (p.crumble)  { fill = rgb565(215, 190, 145); edge = rgb565(150, 125, 85); }
    _canvas.fillRoundRect(px, py, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, fill);
    _canvas.drawRoundRect(px, py, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, edge);
    if (p.crumble) {  // crack marks
      const int mx = px + DoodleGame::PLAT_W / 2;
      _canvas.drawLine(mx - 4, py + 1, mx, py + DoodleGame::PLAT_H - 2, edge);
      _canvas.drawLine(mx, py + DoodleGame::PLAT_H - 2, mx + 5, py + 1, edge);
    }
    if (p.spring) {  // spring boost marker on top of the platform
      const int sx = px + DoodleGame::PLAT_W / 2 - 4, sy = py - 6;
      _canvas.fillRect(sx, sy, 8, 6, rgb565(235, 150, 40));
      _canvas.drawFastHLine(sx, sy + 1, 8, rgb565(120, 70, 20));
      _canvas.drawFastHLine(sx, sy + 4, 8, rgb565(120, 70, 20));
    }
  }

  drawDoodleFerret((int)game.ferretX(), (int)game.ferretY() + 6, game.faceLeft());

  // score (top center) + record (top right, small)
  _canvas.setTextColor(rgb565(30, 50, 70));
  _canvas.setTextSize(2);
  char sc[12];
  snprintf(sc, sizeof(sc), "%d", game.score());
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sc) / 2, 8);
  _canvas.print(sc);
  if (game.hiScore() > 0) {
    _canvas.setTextSize(1);
    char hs[16];
    snprintf(hs, sizeof(hs), "rec %d", game.hiScore());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(hs) / 2, 26);
    _canvas.print(hs);
  }

  // back button (top-left)
  _canvas.fillCircle(18, 16, 12, BTN_BG);
  _canvas.drawCircle(18, 16, 12, BTN_BORDER);
  _canvas.fillTriangle(22, 11, 22, 21, 14, 16, BTN_BORDER);

  if (game.gameOver()) {
    _canvas.fillRoundRect(34, 86, 172, 72, 10, menu::CLOCK_BG);
    _canvas.drawRoundRect(34, 86, 172, 72, 10, menu::CLOCK_EDGE);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    const char* go = "Fim!";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(go) / 2, 94);
    _canvas.print(go);
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CLOCK_DATE);
    char l[24];
    snprintf(l, sizeof(l), "Score: %d", game.score());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l) / 2, 116);
    _canvas.print(l);
    if (game.newRecord()) {
      _canvas.setTextColor(rgb565(250, 210, 60));
      const char* nr = "NOVO RECORDE!";
      _canvas.setCursor(CENTER_X - _canvas.textWidth(nr) / 2, 130);
      _canvas.print(nr);
      _canvas.setTextColor(menu::CLOCK_DATE);
    } else {
      char r[24];
      snprintf(r, sizeof(r), "Recorde: %d", game.hiScore());
      _canvas.setCursor(CENTER_X - _canvas.textWidth(r) / 2, 130);
      _canvas.print(r);
    }
    const char* h = "toque p/ voltar";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h) / 2, 144);
    _canvas.print(h);
  }

  _canvas.pushSprite(0, 0);
}

// The game ferret with a mode-appropriate animation, centered at (cx,cy).
// zoom>1 nearest-scales the 40px sprite (used at 2x in Bolinha).
void Renderer::drawGameFerret(int cx, int cy, int mode, bool faceLeft, int zoom) {
  const uint16_t* fr;
  if (mode == 2) {        // jumping (celebration)
    const int idx = (millis() / 80) % FERRET_G_JUMP_FRAMES;
    fr = faceLeft ? &ferret_g_jump_l[idx][0] : &ferret_g_jump[idx][0];
  } else if (mode == 1) { // walking (chase / stroll)
    const int idx = (millis() / 90) % FERRET_G_WALK_FRAMES;
    fr = faceLeft ? &ferret_g_walk_l[idx][0] : &ferret_g_walk[idx][0];
  } else {                // idle (breathing loop)
    const int idx = (millis() / 180) % FERRET_G_IDLE_FRAMES;
    fr = faceLeft ? &ferret_g_idle_l[idx][0] : &ferret_g_idle[idx][0];
  }
  if (zoom <= 1) {
    _canvas.pushImage(cx - FERRET_G_FRAME_W / 2, cy - FERRET_G_FRAME_H / 2,
                      FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                      (uint16_t)FERRET_G_TRANSPARENT_KEY);
  } else {
    _canvas.pushImageRotateZoom(cx, cy, FERRET_G_FRAME_W / 2.0f, FERRET_G_FRAME_H / 2.0f,
                                0.0f, (float)zoom, (float)zoom,
                                FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                                (uint16_t)FERRET_G_TRANSPARENT_KEY);
  }
}

// A tennis ball: yellow disc + rim + the white curved seam.
void Renderer::drawTennisBall(int cx, int cy, int r) {
  _canvas.fillCircle(cx, cy, r, menu::IC_BALL);
  _canvas.drawCircle(cx, cy, r, rgb565(150, 175, 40));
  const int span = r - 2;
  for (int dy = -span; dy <= span; dy++) {  // hourglass seam ")("
    const float f = 1.0f - (float)(dy * dy) / (float)(span * span);
    const int off = (int)((r * 0.45f) * f);
    _canvas.drawPixel(cx - (r - 3) + off, cy + dy, TFT_WHITE);
    _canvas.drawPixel(cx + (r - 3) - off, cy + dy, TFT_WHITE);
  }
}

void Renderer::drawBall(BallGame& game) {
  // Reuse the day forest scene (sky, sun, cabin, pines, grass) as the backdrop.
  _p = DAY;
  drawSky();
  drawSun();
  drawForest(false);
  drawSparkles(false);

  // ferret at the home height (2x = 80px, feet on the grass), then the ball
  const int mode = game.state() == BallGame::CELEBRATE ? 2 : (game.walking() ? 1 : 0);
  drawGameFerret((int)game.ferretX(), 88, mode, game.faceLeft(), 2);
  drawTennisBall((int)game.ballX(), (int)game.ballY(), 8);

  // catch count (top center) on a small dark chip so it reads over the scene
  char sc[12];
  snprintf(sc, sizeof(sc), "%d", game.catches());
  _canvas.setTextSize(2);
  const int scw = _canvas.textWidth(sc);
  _canvas.fillRoundRect(CENTER_X - scw / 2 - 6, 6, scw + 12, 20, 5, menu::CLOCK_BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - scw / 2, 9);
  _canvas.print(sc);

  // left-edge tab: pull right to exit. Chevron ">" points inward, fully
  // inside the tab (x spans -6..RHANDLE_W, so keep the tip at <= RHANDLE_W-3).
  _canvas.fillRoundRect(-6, RHANDLE_CY - RHANDLE_H / 2, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BG);
  _canvas.drawRoundRect(-6, RHANDLE_CY - RHANDLE_H / 2, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BORDER);
  _canvas.fillTriangle(4, RHANDLE_CY - 7, 4, RHANDLE_CY + 7, RHANDLE_W - 3, RHANDLE_CY, BTN_BORDER);

  if (game.ready()) {
    _canvas.setTextSize(1);
    const char* hint = "arraste p/ lancar";
    const int hw = _canvas.textWidth(hint);
    _canvas.fillRoundRect(CENTER_X - hw / 2 - 5, 201, hw + 10, 13, 4, menu::CLOCK_BG);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(CENTER_X - hw / 2, 204);
    _canvas.print(hint);
  }

  _canvas.pushSprite(0, 0);
}

void Renderer::drawFerret(FerretActor& ferret) {
  const uint16_t* fr = ferret.frame();
  if (!fr) return;
  _canvas.pushImage(ferret.x(), ferret.y(), ferret.w(), ferret.h(),
                    fr, ferret.transparentKey());
}

void Renderer::drawStatBar(int x, int y, const char* label, float value) {
  _canvas.setTextSize(1);
  _canvas.setTextColor(_p.textDim);
  _canvas.setCursor(x, y);
  _canvas.print(label);

  const int barX = x + 30, barW = 60, barH = 8;
  _canvas.fillRoundRect(barX, y - 1, barW, barH, 2, _p.barBg);
  int fillW = (int)(barW * (value / 100.0f));
  uint16_t color = value < 25 ? BAR_LOW : (value < 55 ? BAR_MID : BAR_HIGH);
  if (fillW > 0) _canvas.fillRoundRect(barX, y - 1, fillW, barH, 2, color);
}

void Renderer::drawIcon(ButtonId id, int cx, int cy) {
  switch (id) {
    case BTN_FEED:  // apple
      _canvas.fillCircle(cx - 4, cy, 7, ICON_APPLE);
      _canvas.fillCircle(cx + 4, cy, 7, ICON_APPLE);
      _canvas.fillTriangle(cx - 1, cy - 10, cx + 3, cy - 10, cx + 1, cy - 4, ICON_LEAF);
      break;
    case BTN_PAT:  // paw
      _canvas.fillCircle(cx, cy + 3, 6, ICON_PAW);
      _canvas.fillCircle(cx - 6, cy - 5, 3, ICON_PAW);
      _canvas.fillCircle(cx, cy - 8, 3, ICON_PAW);
      _canvas.fillCircle(cx + 6, cy - 5, 3, ICON_PAW);
      break;
    case BTN_SLEEP:  // crescent moon
      _canvas.fillCircle(cx, cy, 9, ICON_MOON);
      _canvas.fillCircle(cx + 5, cy - 3, 8, BTN_BG);
      break;
    case BTN_CLEAN:  // water drop
      _canvas.fillCircle(cx, cy + 3, 7, ICON_DROP);
      _canvas.fillTriangle(cx - 7, cy + 1, cx + 7, cy + 1, cx, cy - 10, ICON_DROP);
      _canvas.fillCircle(cx - 2, cy, 2, ICON_DROP_HL);
      break;
    default:
      break;
  }
}

void Renderer::drawButtons() {
  const unsigned long now = millis();
  for (int i = 0; i < BTN_COUNT; i++) {
    const auto& b = BUTTONS[i];
    bool pressed = (_pressedButton == i && now < _pressedUntil);
    uint16_t bg = pressed ? BTN_BG_PRESSED : BTN_BG;
    _canvas.fillCircle(b.cx, b.cy, BUTTON_RADIUS, bg);
    _canvas.drawCircle(b.cx, b.cy, BUTTON_RADIUS, BTN_BORDER);
    drawIcon((ButtonId)i, b.cx, b.cy);
  }
}

// A small battery pill (icon + %). Placed high but INSIDE the visible circle
// (the corners are hidden behind the round bezel). Reused by the pet scene and
// the config menu; `outline`/`txt` adapt it to each background.
void Renderer::drawBatteryPill(int topY, int pct, uint16_t outline, uint16_t txt) {
  const uint16_t lvl = pct <= 20 ? rgb565(230, 70, 60)
                     : pct <= 50 ? rgb565(240, 190, 60)
                                 : rgb565(90, 200, 110);
  char t[6];
  snprintf(t, sizeof(t), "%d%%", pct);
  _canvas.setTextSize(1);
  const int tw = _canvas.textWidth(t);
  const int total = 20 + 3 + tw;  // 18px body + 2px nub + gap + text
  const int bx = CENTER_X - total / 2, by = topY;
  _canvas.drawRoundRect(bx, by, 18, 9, 2, outline);
  _canvas.fillRect(bx + 18, by + 3, 2, 3, outline);            // nub
  _canvas.fillRect(bx + 2, by + 2, (14 * pct) / 100, 5, lvl);  // level
  _canvas.setTextColor(txt);
  _canvas.setCursor(bx + 20 + 3, by + 1);
  _canvas.print(t);
}


void Renderer::draw(const Pet& pet, Battery& battery, FerretActor& ferret,
                    bool menuOpen, ui::MenuPage menuPage, int volume, int ledBright,
                    bool wifiOn, const char* ip, bool clockActive, Clock& clock) {
  // Theme follows the real time of day (06-16 day, 16-18 afternoon, else
  // night). Without a synced clock, fall back to the pet's sleep state.
  enum { TOD_DAY, TOD_AFTERNOON, TOD_NIGHT } tod;
  const int h = clock.localHour();
  if (h < 0) {
    tod = TOD_DAY;  // no synced clock -> always day
  } else if (h >= 6 && h < 16) {
    tod = TOD_DAY;
  } else if (h >= 16 && h < 18) {
    tod = TOD_AFTERNOON;
  } else {
    tod = TOD_NIGHT;
  }

  const bool night = (tod == TOD_NIGHT);
  _p = (tod == TOD_NIGHT) ? NIGHT : (tod == TOD_AFTERNOON ? AFTERNOON : DAY);

  drawSky();
  if (tod == TOD_NIGHT) {
    drawStars();
    drawMoon();
  } else if (tod == TOD_AFTERNOON) {
    drawSunset();
  } else {
    drawSun();
  }
  drawForest(night);
  drawSparkles(night);
  drawHeader(pet, wifiOn);
  drawFerret(ferret);
  // battery is shown in the config menu (not the home scene)

  if (clockActive) {
    // idle mode: clock replaces bars + buttons
    drawClock(clock);
  } else {
    drawStatBar(14,  136, "FOME", pet.hunger());
    drawStatBar(126, 136, "ALEG", pet.joy());
    drawStatBar(14,  150, "ENER", pet.energy());
    drawStatBar(126, 150, "HIGI", pet.hygiene());
    drawButtons();
    if (menuOpen) {
      drawMenu(menuPage, volume, ledBright, battery.percent(), wifiOn, ip);
    } else {
      drawMenuHandle();   // config menu (top)
      drawRightHandle();  // games menu (right)
    }
  }

  _canvas.pushSprite(0, 0);
}
