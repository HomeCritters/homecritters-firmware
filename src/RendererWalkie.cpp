#include "Renderer.h"
#include "RendererShared.h"
#include "Walkie.h"

// Renderer partial: the walkie-talkie screens - the friends list (toggle +
// "Todos" broadcast + discovered peers) and the Apple Watch-style talk screen
// (big yellow hold-to-talk circle).

using namespace theme;
using namespace ui;

// One friends-list row: rounded pill with an icon and a (scrolling) name.
static void wtRow(LGFX_Sprite& cv, Renderer* r, int y, const char* label,
                  bool broadcast, bool dim) {
  const uint16_t bg = dim ? rgb565(38, 34, 52) : rgb565(96, 78, 30);
  const uint16_t border = dim ? BTN_BORDER : rgb565(245, 165, 25);
  cv.fillRoundRect(WT_ROW_X, y, WT_ROW_W, WT_ROW_H, 12, bg);
  cv.drawRoundRect(WT_ROW_X, y, WT_ROW_W, WT_ROW_H, 12, border);
  const int ix = WT_ROW_X + 22, iy = y + WT_ROW_H / 2;
  if (broadcast) {  // megaphone
    cv.fillTriangle(ix - 8, iy, ix + 2, iy - 7, ix + 2, iy + 7, rgb565(250, 205, 80));
    cv.fillRect(ix + 2, iy - 3, 4, 6, rgb565(250, 205, 80));
    cv.drawFastHLine(ix + 8, iy - 5, 4, rgb565(255, 235, 150));  // sound waves
    cv.drawFastHLine(ix + 9, iy, 5, rgb565(255, 235, 150));
    cv.drawFastHLine(ix + 8, iy + 5, 4, rgb565(255, 235, 150));
  } else {          // little radio
    cv.fillRoundRect(ix - 5, iy - 6, 10, 14, 3, rgb565(250, 205, 80));
    cv.drawFastVLine(ix - 3, iy - 10, 4, rgb565(250, 205, 80));  // antenna
    for (int g = 0; g < 2; g++)
      cv.drawFastHLine(ix - 3, iy - 2 + g * 4, 6, rgb565(120, 80, 15));
  }
}

void Renderer::drawWalkieList(Walkie& wt) {
  beginScreen(menu::BG);
  drawPageHeader("Walkie", wt.enabled() ? "" : "desligado");
  drawSwitchIcon(WT_TOGGLE_CX, WT_TOGGLE_CY, wt.enabled());

  const bool dim = !wt.enabled();
  // Row 0: broadcast to everyone. Rows 1..2: first discovered peers (v1 fits
  // the row layout; MAX_PEERS is bigger but two devices is today's world).
  wtRow(_canvas, this, WT_ROW0_Y, "Todos", true, dim);
  _canvas.setTextColor(dim ? menu::TEXT_DIM : TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(WT_ROW_X + 42, WT_ROW0_Y + WT_ROW_H / 2 - 7);
  _canvas.print("Todos");

  const int shown = wt.peerCount() < 2 ? wt.peerCount() : 2;
  for (int i = 0; i < shown; i++) {
    const int y = WT_ROW0_Y + (i + 1) * (WT_ROW_H + 4);
    wtRow(_canvas, this, y, wt.peer(i).name, false, dim);
    _canvas.setTextColor(dim ? menu::TEXT_DIM : TFT_WHITE);
    _canvas.setTextSize(2);
    drawScrollText(WT_ROW_X + 40, y + WT_ROW_H / 2 - 7, WT_ROW_W - 50,
                   wt.peer(i).name, dim ? menu::TEXT_DIM : TFT_WHITE, 2);
  }
  if (shown == 0) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::TEXT_DIM);
    const char* s = wt.scanning() ? "procurando..." : "nenhum amigo na rede";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(s) / 2, WT_ROW0_Y + 58);
    _canvas.print(s);
    if (wt.scanning()) {  // little spinner dots
      const int ph = (int)((millis() / 220) % 3);
      for (int i = 0; i < 3; i++)
        _canvas.fillCircle(CENTER_X - 12 + i * 12, WT_ROW0_Y + 76,
                           i == ph ? 3 : 2,
                           i == ph ? rgb565(250, 205, 80) : menu::TEXT_DIM);
    }
  }
  drawLeftHandle();
  endScreen();
}

void Renderer::drawWalkieTalk(Walkie& wt, const char* target, bool pressed) {
  beginScreen(menu::BG);
  const bool rx = wt.state() == WT_RX;
  const unsigned long ms = millis();

  // Target name up top (yellow, Apple Watch style).
  _canvas.setTextSize(2);
  _canvas.setTextColor(rgb565(250, 205, 80));
  drawScrollText(60, 22, 120, target, rgb565(250, 205, 80), 2);

  // The big circle. Idle: yellow. Held (TX): brighter + pulsing ring.
  // Receiving: green + pulsing ring + "ouvindo".
  uint16_t body = menu::IC_SUN;
  if (pressed) body = menu::IC_SUN2;
  if (rx) body = rgb565(70, 190, 90);
  _canvas.fillCircle(WT_BTN_CX, WT_BTN_CY, WT_BTN_R, body);
  _canvas.drawCircle(WT_BTN_CX, WT_BTN_CY, WT_BTN_R, rgb565(255, 235, 150));
  if (pressed || rx) {  // breathing outer ring while the channel is open
    const int rr = WT_BTN_R + 4 + (int)(3.0f * (0.5f + 0.5f * sinf(ms / 180.0f)));
    _canvas.drawCircle(WT_BTN_CX, WT_BTN_CY, rr,
                       rx ? rgb565(120, 230, 140) : rgb565(255, 235, 150));
    _canvas.drawCircle(WT_BTN_CX, WT_BTN_CY, rr + 1,
                       lerp565(rx ? rgb565(120, 230, 140) : rgb565(255, 235, 150),
                               menu::BG, 0.5f));
  }

  // Radio glyph in the circle center + label under it.
  {
    const int cx = WT_BTN_CX, cy = WT_BTN_CY - 14;
    const uint16_t ink = rgb565(60, 42, 8);
    _canvas.fillRoundRect(cx - 10, cy - 9, 20, 27, 5, ink);
    _canvas.fillRoundRect(cx - 7, cy - 18, 3, 10, 1, ink);   // antenna
    _canvas.fillCircle(cx, cy + 1, 6, body);                 // speaker hole
    _canvas.fillCircle(cx, cy + 1, 3, ink);
    _canvas.setTextSize(2);
    _canvas.setTextColor(ink);
    const char* l1 = rx ? "OUVINDO" : (pressed ? "FALE!" : "SEGURE");
    _canvas.setCursor(cx - _canvas.textWidth(l1) / 2, WT_BTN_CY + 20);
    _canvas.print(l1);
    if (!pressed && !rx) {
      _canvas.setTextSize(1);
      const char* l2 = "para falar";
      _canvas.setCursor(cx - _canvas.textWidth(l2) / 2, WT_BTN_CY + 40);
      _canvas.print(l2);
    }
  }
  // Receiving: who's talking, under the circle.
  if (rx) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(rgb565(120, 230, 140));
    _canvas.setCursor(CENTER_X - _canvas.textWidth(wt.rxName()) / 2, 218);
    _canvas.print(wt.rxName());
  }
  drawLeftHandle();
  endScreen();
}
