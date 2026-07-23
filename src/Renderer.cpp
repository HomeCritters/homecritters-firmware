#include "Renderer.h"
#include <cstring>
#include <Preferences.h>
#include "GameConfig.h"
#include "RendererShared.h"  // GROUND_Y + lerp565 (shared with the partials)

// Renderer core: lifecycle, the pet scene (forest, weather FX, HUD, clock,
// media/voice overlays) and shared text helpers. The full-screen pages live
// in the partials: RendererMenus / RendererHa / RendererWeatherScreen /
// RendererGames (same class, split by screen domain).

using namespace theme;
using namespace ui;

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

// --- Screen lifecycle -------------------------------------------------------
// Every full-screen draw path starts with beginScreen (background fill) and
// MUST end with endScreen on every return path: it pushes the canvas to the
// LCD. Forgetting the push freezes the physical display on the last frame
// while screenshots (which read the CANVAS, not the LCD) keep showing the new
// screen looking perfect - a bug class this codebase already shipped once
// (drawHaPanel). Route every push through here; never call pushSprite direct.
void Renderer::beginScreen(uint16_t bg) { _canvas.fillScreen(bg); }
void Renderer::endScreen() { _canvas.pushSprite(0, 0); }

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
  if (!_displayOff) _lcd.setBrightness(map(_scrBright, 0, 100, 0, 255));
  // NVS write is debounced (flushNvs): a portal slider drag fires this dozens
  // of times and each write blocks the render loop on a flash commit.
  _nvsDirtyAt = millis() | 1;
}

void Renderer::flushNvs() {
  if (!_nvsDirtyAt || millis() - _nvsDirtyAt < 2000) return;
  _nvsDirtyAt = 0;
  Preferences p;
  p.begin("disp", false);
  p.putInt("scr", _scrBright);
  p.end();
}

// Full-sleep display blank: backlight hard off (bypasses the brightness
// floor), restored to the saved brightness on wake. Not persisted - a reboot
// always wakes the screen (so it can never get stuck dark).
void Renderer::setDisplayOff(bool off) {
  _displayOff = off;
  _lcd.setBrightness(off ? 0 : map(_scrBright, 0, 100, 0, 255));
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

// ---- Real-weather scene effects --------------------------------------------

void Renderer::drawHeader(const Pet& pet, bool wifiOn, bool micMuted,
                          bool micLive) {
  const Mood mood = pet.mood();
  _canvas.setTextColor(_p.text);
  _canvas.setTextSize(2);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(pet.name().c_str()) / 2, 20);
  _canvas.print(pet.name());

  // WiFi indicator: only when there's something to say. The old "connected"
  // dot read as a tiny moon floating over the sunset - gone. Disconnected
  // shows a small crossed-out WiFi glyph (mirrors the mic icon's spot).
  if (!wifiOn) {
    const int wx = 182, wy = 48;
    const uint16_t c = _canvas.color565(200, 200, 210);
    _canvas.fillCircle(wx, wy, 2, c);  // emitter dot
    for (int b = 1; b <= 2; b++) {     // two arcs above it
      const int r = 3 + b * 4;
      for (int deg = -55; deg <= 55; deg += 9) {
        const float a = deg * 3.14159f / 180.0f;
        _canvas.drawPixel(wx + (int)(r * sinf(a)), wy - (int)(r * cosf(a)), c);
      }
    }
    const uint16_t red = _canvas.color565(235, 40, 40);
    _canvas.drawLine(wx - 7, wy - 10, wx + 7, wy + 4, red);
    _canvas.drawLine(wx - 7, wy - 9, wx + 8, wy + 4, red);
  }

  // Mic muted: small crossed-out mic top-left. The LED stays on mood duty -
  // this is the on-screen privacy hint. Position pulled INSIDE the round
  // glass: at (44,38) the slash corner sat at r~123 from center, past the
  // 119.5 bezel - it looked fine on the square canvas but was clipped on
  // the physical display. Worst corner now sits at r~109.
  if (micMuted || micLive) {
    // Same little mic glyph either way: muted = white + bold red slash
    // (privacy); live = soft cyan, no slash (the assistant can hear - HA
    // satellite connected and the mic stream is flowing).
    const int mx = 58, my = 46;
    const uint16_t body = micMuted ? _canvas.color565(232, 232, 236)
                                   : _canvas.color565(90, 210, 235);
    _canvas.fillRoundRect(mx - 2, my - 7, 5, 9, 2, body);  // capsule
    _canvas.drawFastVLine(mx - 4, my - 1, 5, body);        // holder U
    _canvas.drawFastVLine(mx + 4, my - 1, 5, body);
    _canvas.drawFastHLine(mx - 4, my + 4, 9, body);
    _canvas.drawFastVLine(mx, my + 5, 3, body);            // stem
    _canvas.drawFastHLine(mx - 2, my + 8, 5, body);        // base
    if (micMuted) {
      const uint16_t red = _canvas.color565(235, 40, 40);  // bold red slash
      _canvas.drawLine(mx - 6, my - 9, mx + 6, my + 9, red);
      _canvas.drawLine(mx - 5, my - 9, mx + 7, my + 9, red);
      _canvas.drawLine(mx - 6, my - 8, mx + 6, my + 10, red);
    }
  }

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

  // Current weather under the clock (only with real data): mini condition
  // icon + temperature, centered in the strip between the panel (ends 200)
  // and the bottom tab (starts 226).
  if (_wxTemp != INT8_MIN) {
    char tw[6];
    snprintf(tw, sizeof(tw), "%d", _wxTemp);
    _canvas.setTextSize(2);
    const int textW = _canvas.textWidth(tw);
    const int total = 18 + 8 + textW + 8;      // icon + gap + temp + degree
    const int x0 = CENTER_X - total / 2;
    drawWxIcon(x0 + 9, 216, _wxCode, 1);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(x0 + 26, 209);
    _canvas.print(tw);
    _canvas.drawCircle(x0 + 26 + textW + 4, 210, 2, TFT_WHITE);  // degree
  }
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

void Renderer::drawLeftHandle() {
  // Tab on the left edge with a ">" chevron: pull right (or tap) = back.
  const int ry = RHANDLE_CY - RHANDLE_H / 2;
  _canvas.fillRoundRect(-6, ry, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BG);
  _canvas.drawRoundRect(-6, ry, RHANDLE_W + 6, RHANDLE_H, 6, BTN_BORDER);
  _canvas.fillTriangle(4, RHANDLE_CY - 7, 4, RHANDLE_CY + 7, RHANDLE_W - 3, RHANDLE_CY, BTN_BORDER);
}

void Renderer::drawBottomHandle() {
  // Tab at the bottom center with a "^" chevron: swipe up = weather forecast.
  // Exact mirror of the top handle (54 long, 14 visible, chevron 7px) so all
  // four pull tabs look identical.
  const int by = SCREEN_H - 14;
  _canvas.fillRoundRect(CENTER_X - BHANDLE_W / 2, by, BHANDLE_W, 20, 6, BTN_BG);
  _canvas.drawRoundRect(CENTER_X - BHANDLE_W / 2, by, BHANDLE_W, 20, 6, BTN_BORDER);
  _canvas.fillTriangle(CENTER_X - 7, SCREEN_H - 4, CENTER_X + 7, SCREEN_H - 4,
                       CENTER_X, SCREEN_H - 11, BTN_BORDER);
}

// One HA tile: controllable tiles glow by on/off + toggle on tap; sensor tiles
// show the value big. Icon by domain (temp/humidity inferred from the value).
void Renderer::drawScrollText(int x, int y, int w, const char* s,
                              uint16_t color, uint8_t size) {
  _canvas.setTextSize(size);
  _canvas.setTextColor(color);
  const int tw = _canvas.textWidth(s);
  if (tw <= w) {  // fits: center it, no scrolling
    _canvas.setCursor(x + (w - tw) / 2, y);
    _canvas.print(s);
    return;
  }
  // Too wide: clip to the box and BOUNCE (ping-pong). Slide left to reveal the
  // end, hold, slide back to the start, hold - no wrap, no seam. millis()-driven
  // so every scrolling label shares one clock and keeps moving as it redraws.
  const int maxOff = tw - w;         // how far to travel to show the tail
  const int msPerPx = 33;            // ~30 px/s (calm, readable glide)
  const int pause = 900;             // dwell at each end (ms)
  const int travel = maxOff * msPerPx;
  const int period = 2 * (travel + pause);
  const int t = (int)(millis() % period);
  int off;
  if (t < pause)                    off = 0;                          // hold start
  else if (t < pause + travel)      off = (t - pause) / msPerPx;      // -> reveal end
  else if (t < 2 * pause + travel)  off = maxOff;                     // hold end
  else                              off = maxOff - (t - 2 * pause - travel) / msPerPx;
  off = constrain(off, 0, maxOff);
  _canvas.setClipRect(x, y, w, _canvas.fontHeight());
  // Text wrap must be OFF: with it on, print() re-homes a cursor that starts
  // outside the clip area, silently pinning the text at x (frozen marquee).
  _canvas.setTextWrap(false);
  _canvas.setCursor(x - off, y);
  _canvas.print(s);
  _canvas.setTextWrap(true);
  _canvas.clearClipRect();
}

// ---- Weather forecast screen -----------------------------------------------

// ------------------- Games -------------------

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


void Renderer::draw(const Pet& pet, Battery& battery, FerretActor& ferret,
                    bool menuOpen, ui::MenuPage menuPage, int volume, int ledBright,
                    bool wifiOn, const char* ip, bool clockActive, Clock& clock,
                    uint8_t mediaFx, uint8_t voiceState, bool micMuted,
                    bool micLive) {
  // An active pairing takes over the whole screen (TV-pairing style).
  if (_pairPin[0]) {
    drawPairingOverlay();
    endScreen();
    return;
  }
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

  // Party mode is a NIGHT club: music forces the night theme (purple sky,
  // stars, moon, fireflies) regardless of the real hour.
  if (mediaFx == 1 && !menuOpen) tod = TOD_NIGHT;

  const bool night = (tod == TOD_NIGHT);
  _p = (tod == TOD_NIGHT) ? NIGHT : (tod == TOD_AFTERNOON ? AFTERNOON : DAY);

  // Real weather paints on top of the time-of-day theme. Every WMO variant
  // gets its own look: partly-cloudy keeps the sun with a light tint, rain
  // scales with intensity, freezing rain goes icy, snow flakes scale, hail
  // pelts, storms strike visible lightning. Party mode stays untouched.
  const bool party = (mediaFx == 1 && !menuOpen);
  const uint8_t wc = party ? 0 : _wxCode;
  const WxKind k = Weather::kindFromCode(wc);
  const uint8_t inten = Weather::intensityFromCode(wc);
  const bool freezing = Weather::codeFreezing(wc);
  const bool hail = Weather::codeHail(wc);
  const bool partly = (wc == 2);           // sun still visible
  const bool wisp = (wc == 1);             // mainly clear: one stray cloud
  const bool overcast = k != WX_CLEAR && !partly;

  uint8_t tint = 0, cloudN = 0;
  if (partly) { tint = 40; cloudN = 2; }
  else if (wc == 3) { tint = 85; cloudN = 3; }
  else if (k == WX_FOG) { tint = 95; cloudN = 2; }
  else if (k == WX_SNOW) { tint = 100 + inten * 10; cloudN = 3; }
  else if (k == WX_RAIN) { tint = freezing ? 115 : (uint8_t)(110 + inten * 15); cloudN = 3; }
  else if (k == WX_STORM) { tint = hail ? 145 : 135; cloudN = 3; }
  if (tint) _p = tintPalette(_p, tint);

  drawSky();
  if (k == WX_STORM) {
    // Lightning: schedule a strike every 8-20 s; the strike whites the sky
    // for 2 frames and draws a jagged bolt for ~6 frames (visible!).
    const unsigned long nowMs = millis();
    if (_nextFlashMs == 0) _nextFlashMs = nowMs + 4000;
    if ((long)(nowMs - _nextFlashMs) >= 0) {
      _flashFrames = 6;
      _thunderPending = true;
      _boltX = 40 + (esp_random() % 160);
      _boltSeed = esp_random() | 1;
      _nextFlashMs = nowMs + 8000 + (esp_random() % 12000);
    }
    if (_flashFrames >= 5)  // first 2 frames: sky whiteout
      _canvas.fillRect(0, 0, SCREEN_W, GROUND_Y, rgb565(235, 240, 250));
  }
  if (overcast || wisp) drawClouds(overcast ? cloudN : 1);
  if (!overcast) {  // clear / mainly-clear / partly keep the celestial bodies
    if (tod == TOD_NIGHT) {
      drawStars();
      drawMoon();
    } else if (tod == TOD_AFTERNOON) {
      drawSunset();
    } else {
      drawSun();
    }
  }
  if (k == WX_STORM && _flashFrames > 0) {
    _flashFrames--;
    drawLightning();
  }
  drawForest(night);
  // Seasonal decorations attach to the forest (cabin/pines/grass); the bed
  // spawns under Leon only while he naps.
  if (_fest == FEST_NATAL) drawXmasDecor(night);
  else if (_fest == FEST_HALLOWEEN) drawHalloweenDecor(night);
  else if (_fest == FEST_JUNINA) drawJuninaDecor();
  if (ferret.inBed()) drawBed(ferret);
  drawSparkles(night);
  // Clear-sky critters: green fireflies by night, butterflies by day
  // (weather grounds both; the party has its own light show). Clear nights
  // also get the occasional shooting star.
  if (k == WX_CLEAR && !party) {
    if (night) {
      drawFireflies();
      drawShootingStar();
    } else {
      drawButterflies();
    }
  }
  // Disco floor goes UNDER the pet (Leon dances on it); the ball + lasers
  // overlay goes on top of everything at the end.
  if (party) drawDiscoFloor();
  drawHeader(pet, wifiOn, micMuted, micLive);
  drawFerret(ferret);
  if (ferret.inBed()) drawBlanket(ferret);  // tucked in: blanket over the sprite
  drawHat(ferret);                          // nightcap/party/festive (may no-op)
  if (_bdayMode) drawParty();               // balloons + confetti in front
  // Precipitation/haze falls IN FRONT of the pet (livelier), under the HUD.
  if (k == WX_RAIN) drawRain(inten, freezing);
  if (k == WX_STORM) drawRain(hail ? 1 : 2, false);
  if (hail) drawHail();
  if (k == WX_SNOW) drawSnow(inten, wc == 77, wc == 85 || wc == 86);
  if (k == WX_FOG) drawFog();
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
      drawLeftHandle();   // Home Assistant panel (left)
      drawBottomHandle(); // weather forecast (bottom)
    }
  }

  // Media/voice overlay on top of everything (skip while the config menu
  // covers the screen). Music gets the party show; the assistant voice states
  // (listening/thinking/speaking) get their own ring so there's never a gap in
  // feedback between releasing the button and hearing the reply.
  if (!menuOpen) {
    if (mediaFx == 1) drawMediaFx(1);           // music party show
    else if (voiceState) drawVoiceRing(voiceState);
  }

  endScreen();
}
