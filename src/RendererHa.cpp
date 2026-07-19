#include "Renderer.h"
#include "RendererShared.h"

// Renderer partial: the Home Assistant "Casa" panel (tiles + domain icons).

using namespace theme;
using namespace ui;

// ------------------- HA panel icons + tiles -------------------
void Renderer::drawThermoIcon(int cx, int cy) {
  const uint16_t body = rgb565(220, 225, 235), red = rgb565(230, 70, 60);
  _canvas.fillRoundRect(cx - 3, cy - 12, 6, 16, 3, body);   // stem
  _canvas.drawRoundRect(cx - 3, cy - 12, 6, 16, 3, rgb565(120, 130, 150));
  _canvas.fillCircle(cx, cy + 6, 5, red);                    // bulb
  _canvas.fillRect(cx - 1, cy - 8, 2, 13, red);              // mercury
}

void Renderer::drawDropIcon(int cx, int cy) {
  const uint16_t blue = rgb565(70, 160, 235);
  _canvas.fillTriangle(cx, cy - 12, cx - 7, cy + 2, cx + 7, cy + 2, blue);  // top
  _canvas.fillCircle(cx, cy + 3, 7, blue);                                   // bottom
  _canvas.fillCircle(cx - 2, cy + 2, 2, rgb565(180, 220, 255));             // glint
}

void Renderer::drawSwitchIcon(int cx, int cy, bool on) {
  const uint16_t track = on ? rgb565(70, 200, 90) : rgb565(90, 95, 110);
  _canvas.fillRoundRect(cx - 12, cy - 6, 24, 13, 6, track);      // track
  _canvas.fillCircle(on ? cx + 6 : cx - 6, cy, 5, TFT_WHITE);    // knob
}

void Renderer::drawFanIcon(int cx, int cy) {
  const uint16_t c = rgb565(120, 190, 235);
  for (int a = 0; a < 3; a++) {
    const float t = a * 2.094f;  // 120 deg
    const int ex = cx + (int)(9 * sinf(t)), ey = cy - (int)(9 * cosf(t));
    _canvas.fillTriangle(cx, cy, cx + (int)(5 * sinf(t + 0.5f)),
                         cy - (int)(5 * cosf(t + 0.5f)), ex, ey, c);
  }
  _canvas.fillCircle(cx, cy, 2, rgb565(80, 130, 170));
}

// Presence/motion sensor: a little person that lights up warm when detected.
void Renderer::drawPersonIcon(int cx, int cy, bool present) {
  const uint16_t c = present ? rgb565(255, 214, 110) : rgb565(110, 114, 132);
  _canvas.fillCircle(cx, cy - 5, 4, c);                    // head
  _canvas.fillRoundRect(cx - 6, cy + 1, 13, 9, 4, c);      // shoulders
  if (present) {                                           // "detected" waves
    const uint16_t w = rgb565(232, 190, 80);
    _canvas.drawCircle(cx - 10, cy - 3, 3, w);
    _canvas.drawCircle(cx + 10, cy - 3, 3, w);
  }
}

// Illuminance sensor: small sun (amber disc + rays).
void Renderer::drawSunSmallIcon(int cx, int cy) {
  _canvas.fillCircle(cx, cy, 5, menu::IC_SUN);
  _canvas.fillCircle(cx - 2, cy - 2, 2, menu::IC_SUN2);    // highlight
  for (int a = 0; a < 8; a++) {
    const float t = a * 3.14159f / 4.0f;
    _canvas.drawLine(cx + (int)(7 * cosf(t)), cy + (int)(7 * sinf(t)),
                     cx + (int)(10 * cosf(t)), cy + (int)(10 * sinf(t)),
                     menu::IC_SUN);
  }
}

void Renderer::drawHaTile(int x, int y, const HaPanel::Entity& e) {
  const bool binsensor = strcmp(e.domain, "binary_sensor") == 0;
  const bool sensor = !binsensor && e.value[0] != 0 && !e.controllable;
  const bool on = strcmp(e.state, "on") == 0 || strcmp(e.state, "open") == 0 ||
                  strcmp(e.state, "unlocked") == 0 || strcmp(e.state, "playing") == 0;
  const bool presence = binsensor && (!strcmp(e.devclass, "motion") ||
                                      !strcmp(e.devclass, "occupancy") ||
                                      !strcmp(e.devclass, "presence"));
  // Dark tiles throughout (white text reads well); ON controls glow green,
  // active binary sensors (presence detected!) glow warm amber.
  uint16_t bg = rgb565(44, 46, 58), border = BTN_BORDER;
  if (binsensor && on)       { bg = rgb565(96, 78, 30);  border = rgb565(232, 190, 80); }
  else if (!sensor && !binsensor && on) { bg = rgb565(58, 92, 52); border = rgb565(150, 220, 90); }
  _canvas.fillRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, bg);
  _canvas.drawRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, border);
  const int cx = x + MENU_CELL_W / 2;

  if (binsensor) {
    // Presence/motion: a person that lights up; other binary sensors get a
    // status dot. State word under the icon (read-only, tap does nothing).
    if (presence) drawPersonIcon(cx, y + 18, on);
    else {
      _canvas.fillCircle(cx, y + 18, 6, on ? rgb565(232, 190, 80) : rgb565(90, 94, 110));
      _canvas.drawCircle(cx, y + 18, 6, rgb565(150, 155, 175));
    }
    const char* w = presence ? (on ? "Presente" : "Ausente")
                             : (on ? "Ativo" : "Inativo");
    _canvas.setTextSize(1);
    _canvas.setTextColor(on ? TFT_WHITE : menu::TEXT_DIM);
    _canvas.setCursor(cx - _canvas.textWidth(w) / 2, y + 34);
    _canvas.print(w);
  } else if (sensor) {
    const bool lux = strcmp(e.devclass, "illuminance") == 0;
    const bool humid = strchr(e.value, '%') != nullptr;
    if (lux) drawSunSmallIcon(cx, y + 16);
    else if (humid) drawDropIcon(cx, y + 16);
    else drawThermoIcon(cx, y + 16);
    drawScrollText(x + 5, y + 34, MENU_CELL_W - 10, e.value, TFT_WHITE, 2);
  } else {
    const int iy = y + 24;
    const char* d = e.domain;
    if (!strcmp(d, "light")) drawLightIcon(cx, iy, on);
    else if (!strcmp(d, "fan")) drawFanIcon(cx, iy);
    else if (!strcmp(d, "lock")) drawLockIcon(cx, iy);
    else drawSwitchIcon(cx, iy, on);
  }
  // Name at the bottom - scrolls if it's wider than the tile (never overflows).
  drawScrollText(x + 5, y + MENU_CELL_H - 13, MENU_CELL_W - 10, e.name,
                 rgb565(190, 195, 210), 1);
}

// Full HA panel: 2x2 tiles for the current page + dots + empty states.
void Renderer::drawHaPanel(HaPanel& ha, int page, bool loading) {
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Casa";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 6);
  _canvas.print(title);

  const int n = ha.count();
  if (n == 0) {
    if (loading && !ha.everReceived()) {
      // First open, list not in yet: spinning arc + "Carregando..."
      const int a0 = (int)((millis() / 2) % 360);
      _canvas.fillArc(CENTER_X, 104, 16, 12, a0, a0 + 110, rgb565(120, 180, 255));
      _canvas.setTextSize(1);
      _canvas.setTextColor(menu::TEXT_DIM);
      const char* l = "Carregando...";
      _canvas.setCursor(CENTER_X - _canvas.textWidth(l) / 2, 132);
      _canvas.print(l);
    } else {
      _canvas.setTextSize(1);
      _canvas.setTextColor(menu::TEXT_DIM);
      const char* l1 = ha.everReceived() ? "Nenhum dispositivo" : "Home Assistant";
      const char* l2 = ha.everReceived() ? "Configure no app do HA" : "desconectado";
      _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 110);
      _canvas.print(l1);
      _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 124);
      _canvas.print(l2);
    }
    drawLeftHandle();  // pull the left tab to go back
    endScreen();
    return;
  }

  const int pages = (n + ui::HA_PER_PAGE - 1) / ui::HA_PER_PAGE;
  if (page >= pages) page = pages - 1;
  const int base = page * ui::HA_PER_PAGE;
  const int16_t cols[2] = {MENU_COL_L, MENU_COL_R};
  const int16_t rows[2] = {MENU_ROW_1, MENU_ROW_2};
  for (int i = 0; i < ui::HA_PER_PAGE && base + i < n; i++) {
    drawHaTile(cols[i & 1], rows[i >> 1], ha.at(base + i));
  }
  if (pages > 1) {
    // Vertical pager on the RIGHT of the grid (pagination is a vertical
    // swipe, so the indicator reads vertically too): bobbing chevrons with a
    // column of page dots between them.
    const int px = 210;                      // column x (right of the tiles)
    const int cy = MENU_ROW_1 + MENU_CELL_H + 2;  // vertical center (grid gap)
    const int bob = (millis() / 350) % 2;    // gentle 1px nudge
    const uint16_t hc = rgb565(150, 155, 175);
    const int dh = pages * 8 - 4;            // dot column height
    if (page > 0)                            // chevron up: previous page
      _canvas.fillTriangle(px - 5, cy - dh / 2 - 8 - bob,
                           px + 5, cy - dh / 2 - 8 - bob,
                           px, cy - dh / 2 - 13 - bob, hc);
    for (int p = 0; p < pages; p++) {        // dots, top = first page
      _canvas.fillCircle(px, cy - dh / 2 + p * 8, 2,
                         p == page ? TFT_WHITE : menu::TEXT_DIM);
    }
    if (page < pages - 1)                    // chevron down: more below
      _canvas.fillTriangle(px - 5, cy + dh / 2 + 8 + bob,
                           px + 5, cy + dh / 2 + 8 + bob,
                           px, cy + dh / 2 + 13 + bob, hc);
  }
  drawLeftHandle();  // left tab = back to the pet
  endScreen();
}
