#include "Renderer.h"
#include "RendererShared.h"
#include "Walkie.h"

// Renderer partial: the walkie-talkie screens, styled after the Apple Watch
// app the owner sent as reference - pure black background, signature yellow,
// big friend cards with avatars, and the giant talk circle with the label
// CURVED along its inside edge.

using namespace theme;
using namespace ui;

// Apple Walkie-Talkie yellow (#FFD60A-ish) + tones.
static constexpr uint16_t WT_YELLOW = 0xFEA1;   // rgb565(255,214,10)
static constexpr uint16_t WT_YELLOW_HI = 0xFF2D; // pressed (lighter)
static constexpr uint16_t WT_BLACK = 0x0000;
static constexpr uint16_t WT_CARD_GRAY = 0x2124; // dark gray pill
static constexpr uint16_t WT_GREEN = 0x3D8A;     // iOS switch green-ish
static constexpr uint16_t WT_TRANSP = 0xF81F;    // sprite key color

// Text curved along a circle arc (the Apple "PRESSIONE PARA FALAR" look):
// each character renders into a tiny sprite and is pushed rotated so its
// baseline follows the BOTTOM inside of the circle, reading left to right.
static void curvedText(LGFX_Sprite& canvas, int cx, int cy, int r,
                       const char* txt, uint16_t color) {
  static LGFX_Sprite cs;
  static bool init = false;
  if (!init) {
    cs.setColorDepth(16);
    cs.createSprite(8, 10);
    init = true;
  }
  const int n = strlen(txt);
  const float degPerPx = 180.0f / (3.14159265f * r);
  const float step = 7.0f * degPerPx;                // ~7px per size-1 char
  float a = 90.0f + (n - 1) * step / 2.0f;           // start at the left end
  for (int i = 0; i < n; i++) {
    const float rad = a * 3.14159265f / 180.0f;
    const int x = cx + (int)(r * cosf(rad));
    const int y = cy + (int)(r * sinf(rad));
    cs.fillSprite(WT_TRANSP);
    cs.setTextColor(color);
    cs.setTextSize(1);
    cs.setCursor(1, 1);
    cs.print(txt[i]);
    cs.pushRotateZoom(&canvas, x, y, a - 90.0f, 1.0f, 1.0f, WT_TRANSP);
    a -= step;
  }
}

// Round avatar with the friend's initial (yellow on dark, like a contact).
static void wtAvatar(LGFX_Sprite& cv, int cx, int cy, const char* name) {
  cv.fillCircle(cx, cy, 15, rgb565(70, 52, 8));
  cv.drawCircle(cx, cy, 15, rgb565(120, 92, 16));
  cv.setTextSize(2);
  cv.setTextColor(WT_YELLOW);
  char ini = name[0] ? name[0] : '?';
  if (ini >= 'a' && ini <= 'z') ini -= 32;
  cv.setCursor(cx - 5, cy - 7);
  cv.print(ini);
}

void Renderer::drawWalkieList(Walkie& wt) {
  beginScreen(WT_BLACK);
  // Title, Apple-style yellow.
  _canvas.setTextSize(1);
  _canvas.setTextColor(WT_YELLOW);
  const char* title = "Walkie-Talkie";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 22);
  _canvas.print(title);

  // Enable pill: dark rounded row, label + iOS-style switch. Tap toggles.
  _canvas.fillRoundRect(WT_PILL_X, WT_PILL_Y, WT_PILL_W, WT_PILL_H, 16, WT_CARD_GRAY);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(1);
  _canvas.setCursor(WT_PILL_X + 14, WT_PILL_Y + WT_PILL_H / 2 - 4);
  _canvas.print("Walkie-Talkie");
  {  // switch: track + knob
    const int sx = WT_PILL_X + WT_PILL_W - 40, sy = WT_PILL_Y + WT_PILL_H / 2;
    const bool on = wt.enabled();
    _canvas.fillRoundRect(sx, sy - 9, 30, 18, 9, on ? WT_GREEN : rgb565(72, 74, 84));
    _canvas.fillCircle(on ? sx + 21 : sx + 9, sy, 7, TFT_WHITE);
  }

  // Section label.
  _canvas.setTextColor(rgb565(140, 142, 150));
  _canvas.setCursor(WT_ROW_X + 6, WT_ROW0_Y - 12);
  _canvas.print("Amigos");

  const bool dim = !wt.enabled();
  const uint16_t card = dim ? rgb565(90, 78, 12) : WT_YELLOW;
  // Row 0: "Todos" (broadcast). Row 1: first discovered peer.
  {
    const int y = WT_ROW0_Y;
    _canvas.fillRoundRect(WT_ROW_X, y, WT_ROW_W, WT_ROW_H, 14, card);
    _canvas.setTextColor(WT_BLACK);
    _canvas.setTextSize(2);
    _canvas.setCursor(WT_ROW_X + 16, y + WT_ROW_H / 2 - 7);
    _canvas.print("Todos");
    // megaphone badge on the right
    const int ix = WT_ROW_X + WT_ROW_W - 28, iy = y + WT_ROW_H / 2;
    _canvas.fillCircle(ix, iy, 15, rgb565(70, 52, 8));
    _canvas.fillTriangle(ix - 7, iy, ix + 2, iy - 6, ix + 2, iy + 6, WT_YELLOW);
    _canvas.fillRect(ix + 2, iy - 2, 3, 5, WT_YELLOW);
    _canvas.drawFastHLine(ix + 7, iy - 4, 3, WT_YELLOW);
    _canvas.drawFastHLine(ix + 8, iy, 4, WT_YELLOW);
    _canvas.drawFastHLine(ix + 7, iy + 4, 3, WT_YELLOW);
  }
  if (wt.peerCount() > 0) {
    const int y = WT_ROW0_Y + WT_ROW_H + 10;
    _canvas.fillRoundRect(WT_ROW_X, y, WT_ROW_W, WT_ROW_H, 14, card);
    _canvas.setTextColor(WT_BLACK);
    _canvas.setTextSize(2);
    drawScrollText(WT_ROW_X + 16, y + WT_ROW_H / 2 - 7, WT_ROW_W - 62,
                   wt.peer(0).name, WT_BLACK, 2);
    wtAvatar(_canvas, WT_ROW_X + WT_ROW_W - 28, y + WT_ROW_H / 2, wt.peer(0).name);
  } else {
    _canvas.setTextSize(1);
    _canvas.setTextColor(rgb565(140, 142, 150));
    const char* s = wt.scanning() ? "procurando..." : "nenhum amigo na rede";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(s) / 2, WT_ROW0_Y + WT_ROW_H + 24);
    _canvas.print(s);
    if (wt.scanning()) {
      const int ph = (int)((millis() / 220) % 3);
      for (int i = 0; i < 3; i++)
        _canvas.fillCircle(CENTER_X - 12 + i * 12, WT_ROW0_Y + WT_ROW_H + 42,
                           i == ph ? 3 : 2,
                           i == ph ? WT_YELLOW : rgb565(90, 92, 100));
    }
  }
  drawLeftHandle();
  endScreen();
}

void Renderer::drawWalkieTalk(Walkie& wt, const char* target, bool pressed) {
  beginScreen(WT_BLACK);
  const bool rx = wt.state() == WT_RX;
  const unsigned long ms = millis();

  // Friend name, yellow, top center, BIG (the round bezel prefers centered).
  _canvas.setTextSize(2);
  drawScrollText(60, 18, 120, target, WT_YELLOW, 2);

  // The giant circle.
  uint16_t body = pressed ? WT_YELLOW_HI : WT_YELLOW;
  if (rx) body = rgb565(64, 200, 90);
  _canvas.fillCircle(WT_BTN_CX, WT_BTN_CY, WT_BTN_R, body);
  if (pressed || rx) {  // breathing halo while the channel is open
    const int rr = WT_BTN_R + 3 + (int)(3.0f * (0.5f + 0.5f * sinf(ms / 180.0f)));
    _canvas.drawCircle(WT_BTN_CX, WT_BTN_CY, rr,
                       rx ? rgb565(120, 230, 140) : WT_YELLOW_HI);
    _canvas.drawCircle(WT_BTN_CX, WT_BTN_CY, rr + 1,
                       lerp565(rx ? rgb565(120, 230, 140) : WT_YELLOW_HI, WT_BLACK, 0.55f));
  }

  // Walkie glyph in the center (black, like the reference) - big.
  {
    const int cx = WT_BTN_CX, cy = WT_BTN_CY - 10;
    _canvas.fillRoundRect(cx - 17, cy - 16, 34, 46, 8, WT_BLACK);
    _canvas.fillRoundRect(cx - 12, cy - 30, 5, 16, 2, WT_BLACK);  // antenna
    _canvas.fillCircle(cx, cy - 1, 10, body);                     // speaker ring
    _canvas.fillCircle(cx, cy - 1, 6, WT_BLACK);
    _canvas.fillRect(cx - 10, cy + 14, 20, 4, body);              // ptt bar
    _canvas.fillRect(cx - 10, cy + 21, 12, 3, body);              // small bar
  }

  // The signature curved label along the inside of the circle.
  const char* arc = rx ? "R E C E B E N D O" : (pressed ? "F A L E  A G O R A" : "PRESSIONE PARA FALAR");
  curvedText(_canvas, WT_BTN_CX, WT_BTN_CY, WT_BTN_R - 16, arc, WT_BLACK);

  // Receiving: who's talking, small, under the name.
  if (rx) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(rgb565(120, 230, 140));
    _canvas.setCursor(CENTER_X - _canvas.textWidth(wt.rxName()) / 2, 38);
    _canvas.print(wt.rxName());
  }
  drawLeftHandle();  // the app's standard back affordance
  endScreen();
}
