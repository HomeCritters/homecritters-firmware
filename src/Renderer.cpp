#include "Renderer.h"
#include <cstring>
#include <qrcode.h>
#include "GameConfig.h"

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
  _lcd.setBrightness(180);
  _canvas.setColorDepth(16);
  _canvas.createSprite(SCREEN_W, SCREEN_H);
  // Ferret frames are big-endian RGB565; without this brown turns green.
  _canvas.setSwapBytes(true);
}

void Renderer::flashButton(int idx) {
  _pressedButton = idx;
  _pressedUntil = millis() + game::BUTTON_FLASH_MS;
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

  // WiFi indicator (small dot) on the right edge
  if (wifiOn) _canvas.fillCircle(228, 10, 3, _p.sparkle);

  const char* status = "Feliz";
  switch (mood) {
    case MOOD_HAPPY:   status = "Feliz";            break;
    case MOOD_NEUTRAL: status = "Tranquilo";        break;
    case MOOD_SAD:     status = "Triste";           break;
    case MOOD_HUNGRY:  status = "Com fome";         break;
    case MOOD_SLEEPY:  status = "Dormindo";         break;
    case MOOD_DIRTY:   status = "Precisa de banho"; break;
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

// Draws a QR code (version 3, byte mode) centered horizontally.
void Renderer::drawQr(const char* text, int topY) {
  QRCode qr;
  static uint8_t buf[200];  // version 3 needs 107 bytes
  qrcode_initText(&qr, buf, 3, ECC_LOW, text);

  const int scale = 2;
  const int quiet = 3;  // white border (modules)
  const int total = (qr.size + quiet * 2) * scale;
  const int ox = CENTER_X - total / 2;
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

void Renderer::drawMenu(int volume, bool wifiOn, const char* ip) {
  _canvas.fillScreen(menu::BG);  // full-screen dark purple background

  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Config";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  // --- Volume (label right above the track) ---
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  char vtxt[20];
  snprintf(vtxt, sizeof(vtxt), "Volume  %d%%", volume);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(vtxt) / 2, MENU_VOL_MINUS.cy - 20);
  _canvas.print(vtxt);

  _canvas.fillCircle(MENU_VOL_MINUS.cx, MENU_VOL_MINUS.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(MENU_VOL_MINUS.cx - 8, MENU_VOL_MINUS.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillCircle(MENU_VOL_PLUS.cx, MENU_VOL_PLUS.cy, MENU_BTN_R, menu::VOL_BTN);
  _canvas.fillRect(MENU_VOL_PLUS.cx - 8, MENU_VOL_PLUS.cy - 2, 16, 4, TFT_WHITE);
  _canvas.fillRect(MENU_VOL_PLUS.cx - 2, MENU_VOL_PLUS.cy - 8, 4, 16, TFT_WHITE);
  int bx = MENU_VOL_MINUS.cx + MENU_BTN_R + 8;
  int bw = (MENU_VOL_PLUS.cx - MENU_BTN_R - 8) - bx;
  _canvas.fillRoundRect(bx, MENU_VOL_MINUS.cy - 6, bw, 12, 4, menu::VOL_TRACK);
  _canvas.fillRoundRect(bx, MENU_VOL_MINUS.cy - 6, bw * volume / 100, 12, 4, BAR_HIGH);

  // --- WiFi / portal ---
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  if (wifiOn && ip && ip[0]) {
    // Both addresses with the protocol; QR carries the IP (always resolves).
    const char* host = "http://ferret.local";
    char urlIp[40];
    snprintf(urlIp, sizeof(urlIp), "http://%s", ip);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(host) / 2, MENU_QR_TOP - 20);
    _canvas.print(host);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(urlIp) / 2, MENU_QR_TOP - 10);
    _canvas.print(urlIp);
    char qrUrl[40];
    snprintf(qrUrl, sizeof(qrUrl), "http://%s/", ip);
    drawQr(qrUrl, MENU_QR_TOP);
  } else {
    const char* h1 = "Sem WiFi conectado";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h1) / 2, 118);
    _canvas.print(h1);
    const char* h2 = "toque em WiFi p/ configurar";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h2) / 2, 138);
    _canvas.print(h2);
  }

  // --- bottom buttons ---
  drawPillButton(MENU_WIFI_X, MENU_WIFI_Y, MENU_WIFI_W, MENU_WIFI_H,
                 "WiFi", menu::WIFI_BTN);
  drawPillButton(MENU_CLOSE_X, MENU_CLOSE_Y, MENU_CLOSE_W, MENU_CLOSE_H,
                 "Fechar", menu::CLOSE_BTN);
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

void Renderer::drawGamesMenu() {
  _canvas.fillScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Jogos";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 20);
  _canvas.print(title);

  drawPillButton(GAME_BTN_X, GAME_DOODLE_Y, GAME_BTN_W, GAME_BTN_H,
                 "Doodle Jump", rgb565(70, 170, 90));
  drawPillButton(GAME_BTN_X, GAME_BALL_Y, GAME_BTN_W, GAME_BTN_H,
                 "Bolinha", rgb565(90, 90, 110));
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* soon = "(em breve)";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(soon) / 2, GAME_BALL_Y + GAME_BTN_H + 4);
  _canvas.print(soon);

  drawPillButton(GAMES_BACK_X, GAMES_BACK_Y, GAMES_BACK_W, GAMES_BACK_H,
                 "Voltar", menu::CLOSE_BTN);
  _canvas.pushSprite(0, 0);
}

// A small ferret avatar for the game (the 80px sprite is too big here).
void Renderer::drawDoodleFerret(int cx, int cy, bool faceLeft) {
  const uint16_t body = rgb565(120, 82, 52);
  const uint16_t dark = rgb565(90, 60, 38);
  const uint16_t belly = rgb565(205, 185, 155);
  const int hx = cx + (faceLeft ? -7 : 7);  // head offset
  _canvas.fillCircle(cx, cy + 3, 11, body);
  _canvas.fillCircle(cx, cy + 6, 7, belly);
  _canvas.fillCircle(hx, cy - 5, 8, body);
  _canvas.fillCircle(hx - 4, cy - 11, 2, dark);
  _canvas.fillCircle(hx + 4, cy - 11, 2, dark);
  _canvas.fillCircle(hx + (faceLeft ? -3 : 3), cy - 6, 1, TFT_BLACK);  // eye
  _canvas.fillCircle(hx + (faceLeft ? -8 : 8), cy - 3, 1, dark);       // nose
}

void Renderer::drawDoodle(DoodleGame& game) {
  _canvas.fillScreen(rgb565(150, 205, 235));  // light sky

  const auto* plats = game.platforms();
  for (int i = 0; i < DoodleGame::PLAT_COUNT; i++) {
    const auto& p = plats[i];
    if (!p.active) continue;
    _canvas.fillRoundRect((int)p.x, (int)p.y, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, rgb565(70, 175, 80));
    _canvas.drawRoundRect((int)p.x, (int)p.y, DoodleGame::PLAT_W, DoodleGame::PLAT_H, 3, rgb565(45, 120, 55));
  }

  drawDoodleFerret((int)game.ferretX(), (int)game.ferretY() + 13, game.faceLeft());

  // score (top center)
  _canvas.setTextColor(rgb565(30, 50, 70));
  _canvas.setTextSize(2);
  char sc[12];
  snprintf(sc, sizeof(sc), "%d", game.score());
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sc) / 2, 8);
  _canvas.print(sc);

  // back button (top-left)
  _canvas.fillCircle(18, 16, 12, BTN_BG);
  _canvas.drawCircle(18, 16, 12, BTN_BORDER);
  _canvas.fillTriangle(22, 11, 22, 21, 14, 16, BTN_BORDER);

  if (game.gameOver()) {
    _canvas.fillRoundRect(34, 92, 172, 58, 10, menu::CLOCK_BG);
    _canvas.drawRoundRect(34, 92, 172, 58, 10, menu::CLOCK_EDGE);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    const char* go = "Fim!";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(go) / 2, 100);
    _canvas.print(go);
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CLOCK_DATE);
    char l[24];
    snprintf(l, sizeof(l), "Score: %d", game.score());
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l) / 2, 122);
    _canvas.print(l);
    const char* h = "toque p/ voltar";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h) / 2, 134);
    _canvas.print(h);
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

void Renderer::drawBattery(Battery& battery) {
  _canvas.setTextSize(1);
  _canvas.setTextColor(_p.textDim);
  _canvas.setCursor(6, 6);
  _canvas.printf("%d%%", battery.percent());
}

void Renderer::draw(const Pet& pet, Battery& battery, FerretActor& ferret,
                    bool menuOpen, int volume, bool wifiOn, const char* ip,
                    bool clockActive, Clock& clock) {
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
  drawBattery(battery);

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
      drawMenu(volume, wifiOn, ip);
    } else {
      drawMenuHandle();   // config menu (top)
      drawRightHandle();  // games menu (right)
    }
  }

  _canvas.pushSprite(0, 0);
}
