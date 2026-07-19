#include "Renderer.h"
#include "RendererShared.h"

// Renderer partial: the weather FORECAST screen + WMO glyphs. (The weather
// FX painted over the pet scene - rain, bolts, fog - live in Renderer.cpp.)

using namespace theme;
using namespace ui;

// Small weather glyph from primitives. s=1 -> ~16px (day row), s=2 -> ~32px.
// Tiny 8px-font-scale glyphs for inline info rows (the tile-sized thermo/
// drop icons overwhelmed the forecast screen's text).
void Renderer::drawMiniThermo(int x, int cy) {
  _canvas.drawFastVLine(x, cy - 6, 7, rgb565(220, 220, 228));  // stem
  _canvas.fillCircle(x, cy + 3, 2, rgb565(235, 80, 60));       // bulb
}

void Renderer::drawMiniDrop(int x, int cy) {
  const uint16_t c = rgb565(90, 160, 235);
  _canvas.fillTriangle(x - 2, cy, x + 2, cy, x, cy - 5, c);    // tip
  _canvas.fillCircle(x, cy + 1, 2, c);                          // body
}

void Renderer::drawWxIcon(int cx, int cy, uint8_t code, int s) {
  const WxKind k = Weather::kindFromCode(code);
  const bool partly = (code == 1 || code == 2);  // sun peeking behind a cloud
  if (k == WX_CLEAR || partly) {
    const int sx = partly ? cx - 3 * s : cx;   // sun shifts up-left when shared
    const int sy = partly ? cy - 3 * s : cy;
    _canvas.fillCircle(sx, sy, 5 * s, menu::IC_SUN);
    _canvas.fillCircle(sx - 2 * s, sy - 2 * s, 2 * s, menu::IC_SUN2);
    for (int a = 0; a < 8; a++) {
      const float t = a * 3.14159f / 4.0f;
      _canvas.drawLine(sx + (int)(7 * s * cosf(t)), sy + (int)(7 * s * sinf(t)),
                       sx + (int)(9 * s * cosf(t)), sy + (int)(9 * s * sinf(t)),
                       menu::IC_SUN);
    }
    if (!partly) return;
    // small cloud in front of the sun (bigger for code 2 than code 1)
    const int cw = (code == 2) ? 3 : 2;
    _canvas.fillCircle(cx + 2 * s, cy + 3 * s - cw * s / 2, cw * s, theme::CLOUD);
    _canvas.fillCircle(cx + 6 * s, cy + 3 * s, cw * s, theme::CLOUD_DARK);
    _canvas.fillRect(cx, cy + 3 * s, 8 * s, cw * s, theme::CLOUD);
    return;
  }
  if (k == WX_FOG) {
    // Classic fog glyph: stacked horizontal bars.
    for (int i = 0; i < 4; i++) {
      const int w = (i == 1 || i == 2) ? 14 * s : 10 * s;
      _canvas.fillRect(cx - w / 2, cy - 5 * s + i * 3 * s, w, s,
                       i % 2 ? theme::CLOUD : theme::CLOUD_DARK);
    }
    return;
  }
  // Cloud body (shared by CLOUDY/RAIN/STORM/SNOW; wet kinds lift it up).
  const int oy = (k == WX_CLOUDY) ? 0 : -3 * s;
  _canvas.fillCircle(cx - 4 * s, cy + oy + s, 3 * s, theme::CLOUD_DARK);
  _canvas.fillCircle(cx + 4 * s, cy + oy + s, 3 * s, theme::CLOUD);
  _canvas.fillCircle(cx, cy + oy - 2 * s, 4 * s, theme::CLOUD);
  _canvas.fillRect(cx - 6 * s, cy + oy + s, 12 * s, 3 * s, theme::CLOUD);
  if (k == WX_RAIN) {
    // Freezing rain gets icy pale drops; drizzle 2 drops, rain/heavy 3.
    const uint16_t dc =
        Weather::codeFreezing(code) ? rgb565(215, 240, 255) : theme::RAINDROP;
    const int n = Weather::intensityFromCode(code) == 0 ? 1 : 3;
    for (int i = -1; i <= 1; i += (n == 1 ? 2 : 1)) {
      const int rx = cx + i * 4 * s;
      _canvas.drawLine(rx, cy + oy + 5 * s, rx - s, cy + oy + 8 * s, dc);
    }
  } else if (k == WX_STORM) {
    // Two offset triangles read as a lightning bolt.
    _canvas.fillTriangle(cx, cy + oy + 4 * s, cx + 3 * s, cy + oy + 4 * s,
                         cx - s, cy + oy + 9 * s, theme::BOLT);
    _canvas.fillTriangle(cx + 2 * s, cy + oy + 6 * s, cx - s, cy + oy + 9 * s,
                         cx + s, cy + oy + 12 * s, theme::BOLT);
    if (Weather::codeHail(code)) {  // hail: white pellets beside the bolt
      _canvas.fillCircle(cx - 4 * s, cy + oy + 7 * s, s, TFT_WHITE);
      _canvas.fillCircle(cx + 5 * s, cy + oy + 6 * s, s, TFT_WHITE);
    }
  } else if (k == WX_SNOW) {
    // Grains (77) = a tight row of specks; snow = 3 falling flakes.
    if (code == 77) {
      for (int i = -2; i <= 2; i++)
        _canvas.fillCircle(cx + i * 3 * s, cy + oy + 6 * s, s > 1 ? s - 1 : 1,
                           TFT_WHITE);
    } else {
      for (int i = -1; i <= 1; i++)
        _canvas.fillCircle(cx + i * 4 * s, cy + oy + 6 * s + (i ? s : 3 * s),
                           s, TFT_WHITE);
    }
  }
}

void Renderer::drawWeather(Weather& wx) {
  beginScreen(menu::BG);
  // Own compact header (higher than drawPageHeader): frees vertical room
  // for the forecast ladder below.
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(CENTER_X - _canvas.textWidth("Tempo") / 2, 18);
  _canvas.print("Tempo");
  if (wx.hasLocation()) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::TEXT_DIM);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(wx.city()) / 2, 40);
    _canvas.print(wx.city());
  }

  if (!wx.hasLocation()) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::TEXT_DIM);
    const char* l1 = "Sem cidade configurada";
    const char* l2 = "Configure no portal";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 110);
    _canvas.print(l1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 124);
    _canvas.print(l2);
    drawLeftHandle();
    endScreen();
    return;
  }
  if (!wx.everFetched()) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::TEXT_DIM);
    const char* l1 = "Buscando previsao...";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 116);
    _canvas.print(l1);
    drawLeftHandle();
    endScreen();
    return;
  }

  // --- today, big: icon + current temp + condition + hi/lo ---
  drawWxIcon(82, 72, wx.codeNow(), 2);
  char t[8];
  snprintf(t, sizeof(t), "%d", wx.tempNow());
  _canvas.setTextSize(4);
  _canvas.setTextColor(TFT_WHITE);
  const int tw = _canvas.textWidth(t);
  _canvas.setCursor(124, 58);
  _canvas.print(t);
  _canvas.drawCircle(124 + tw + 6, 60, 3, TFT_WHITE);  // degree mark
  // Condition label, then a mini-thermo (today hi/lo) + mini-drop (humidity)
  // row - tiny glyphs matched to the 8px font so nothing collides.
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* lbl = Weather::labelForCode(wx.codeNow());
  _canvas.setCursor(CENTER_X - _canvas.textWidth(lbl) / 2, 98);
  _canvas.print(lbl);
  {
    char hilo[12], hum[6];
    if (wx.dayCount() > 0)
      snprintf(hilo, sizeof(hilo), "%d/%d", wx.day(0).hi, wx.day(0).lo);
    else
      hilo[0] = 0;
    if (wx.humNow() >= 0) snprintf(hum, sizeof(hum), "%d%%", wx.humNow());
    else hum[0] = 0;
    const int wHilo = hilo[0] ? 9 + _canvas.textWidth(hilo) : 0;
    const int wHum = hum[0] ? 9 + _canvas.textWidth(hum) : 0;
    const int gap = (wHilo && wHum) ? 16 : 0;
    int x = CENTER_X - (wHilo + gap + wHum) / 2;
    _canvas.setTextColor(TFT_WHITE);
    if (hilo[0]) {
      drawMiniThermo(x + 2, 115);
      _canvas.setCursor(x + 9, 112);
      _canvas.print(hilo);
      x += wHilo + gap;
    }
    if (hum[0]) {
      drawMiniDrop(x + 2, 115);
      _canvas.setCursor(x + 9, 112);
      _canvas.print(hum);
    }
  }

  // --- next days: same pattern as today (thermo hi/lo + drop rain%) ---
  const int n = min(wx.dayCount() - 1, 4);  // skip today (index 0)
  if (n > 0) {
    const int spacing = 46;
    const int x0 = CENTER_X - ((n - 1) * spacing) / 2;
    for (int i = 0; i < n; i++) {
      const Weather::Day& d = wx.day(i + 1);
      const int cx = x0 + i * spacing;
      _canvas.setTextSize(1);
      _canvas.setTextColor(TFT_WHITE);
      _canvas.setCursor(cx - _canvas.textWidth(d.day) / 2, 134);
      _canvas.print(d.day);
      drawWxIcon(cx, 154, d.code, 1);
      char hilo[12];
      snprintf(hilo, sizeof(hilo), "%d/%d", d.hi, d.lo);
      int w = 9 + _canvas.textWidth(hilo);
      _canvas.setTextColor(TFT_WHITE);
      drawMiniThermo(cx - w / 2 + 2, 179);
      _canvas.setCursor(cx - w / 2 + 9, 176);
      _canvas.print(hilo);
      if (d.pop >= 0) {  // chance of rain, dim (same drop glyph as today)
        char pop[6];
        snprintf(pop, sizeof(pop), "%d%%", d.pop);
        w = 9 + _canvas.textWidth(pop);
        _canvas.setTextColor(menu::TEXT_DIM);
        drawMiniDrop(cx - w / 2 + 2, 195);
        _canvas.setCursor(cx - w / 2 + 9, 192);
        _canvas.print(pop);
      }
    }
  }
  if (!wx.fresh()) {  // API/WiFi down for 3h+: the data shown is old
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::TEXT_DIM);
    const char* w = "dados antigos";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(w) / 2, 216);
    _canvas.print(w);
  }
  drawLeftHandle();
  endScreen();
}
