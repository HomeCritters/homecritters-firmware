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
  // chimney + cozy smoke puffs drifting up (magic forests have warm cabins)
  const int chx = bx + 12, chTop = by - wallH - 9;
  _canvas.fillRect(chx, chTop, 4, 8, _p.treeTrunk);
  {
    const unsigned long ms = millis();
    const uint16_t smoke = lerp565(_p.skyTop, theme::CLOUD, 0.55f);
    for (int i = 0; i < 3; i++) {
      const int cyc = (int)((ms / 90 + i * 9) % 26);   // rises 26px then loops
      if (cyc > 20) continue;                          // gap between puffs
      const int sy = chTop - 3 - cyc;
      const int sx = chx + 2 + (int)(2.0f * sinf(ms / 800.0f + i * 2.1f));
      _canvas.fillCircle(sx, sy, cyc < 12 ? 1 : 2, smoke);  // grows as it rises
    }
  }
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

  // --- grass detail (restrained: a 30px band can't hold much) -----------
  // Depth strip at the horizon, the classic dark tufts, a few lighter
  // blades between them, three swaying stalks, and ONE quiet accent:
  // a glowing mushroom at night / two tiny wildflowers by day.
  _canvas.fillRect(0, GROUND_Y, SCREEN_W, 3, lerp565(_p.ground, _p.groundDark, 0.5f));
  for (int x = 4; x < SCREEN_W; x += 18) {
    int gy = GROUND_Y + 6 + ((x * 5) % 7);
    _canvas.fillTriangle(x - 3, gy, x + 3, gy, x, gy - 6, _p.groundDark);
  }
  const uint16_t blade = lerp565(_p.groundDark, _p.ground, 0.45f);
  for (int x = 24; x < SCREEN_W; x += 44) {  // sparse lighter row
    int gy = GROUND_Y + 15 + ((x * 3) % 8);
    _canvas.fillTriangle(x - 2, gy, x + 2, gy, x, gy - 5, blade);
  }
  // Swaying grass clumps: 3 clumps of 3 curved blades. Each blade bends
  // from the TIP (two segments, the top one travels twice as far), with a
  // slightly different phase per blade so the clump ripples in the breeze.
  for (int c = 0; c < 3; c++) {
    const int bx = 40 + c * 80;
    const int by = GROUND_Y + 12 + ((c * 47) % 10);
    for (int b = 0; b < 3; b++) {
      const int x0 = bx + (b - 1) * 3;
      const int h = 6 + ((b + c) % 3) * 2;  // 6..10 px tall
      const float sway = sinf(millis() / 850.0f + c * 1.9f + b * 0.6f);
      const int tip = (int)(2.5f * sway);
      const uint16_t col = (b == 1) ? blade : _p.groundDark;
      _canvas.drawLine(x0, by, x0 + tip / 2, by - h / 2, col);
      _canvas.drawLine(x0 + tip / 2, by - h / 2, x0 + tip, by - h, col);
    }
  }
  {
    // The mushroom family lives here around the clock: classic RED toadstools
    // by day (static, with a white speck), glowing blue-family caps pulsing
    // on their own clocks after dark.
    struct M { int16_t x, y, w; uint16_t dark, bright; uint16_t period; float ph; };
    static const M ms[3] = {
        {56, (int16_t)(GROUND_Y + 18), 5, rgb565(60, 40, 90), rgb565(150, 110, 235), 1400, 0.0f},
        {143, (int16_t)(GROUND_Y + 14), 3, rgb565(30, 70, 80), rgb565(80, 200, 210), 1750, 1.6f},
        {207, (int16_t)(GROUND_Y + 21), 4, rgb565(30, 50, 100), rgb565(100, 150, 255), 1150, 3.1f},
    };
    const uint16_t red = rgb565(205, 45, 40);
    for (int i = 0; i < 3; i++) {
      const M& m = ms[i];
      _canvas.drawFastVLine(m.x + m.w / 2, m.y + 2, 3, rgb565(180, 175, 165));
      if (night) {
        const float pu = 0.5f + 0.5f * sinf(millis() / (float)m.period + m.ph);
        _canvas.fillRect(m.x, m.y, m.w, 2, lerp565(m.dark, m.bright, pu));
        if (pu > 0.75f)  // faint ground glow at the peak
          _canvas.drawFastHLine(m.x - 1, m.y + 5, m.w + 2, lerp565(_p.ground, m.bright, 0.25f));
      } else {
        _canvas.fillRect(m.x, m.y, m.w, 2, red);           // still red cap
        _canvas.drawPixel(m.x + m.w / 2 - 1, m.y, TFT_WHITE);  // white speck
      }
    }
  }
  if (!night) {
    static const int16_t fl[][2] = {{40, 124}, {186, 121}};
    static const uint16_t fc[] = {rgb565(255, 245, 250), rgb565(250, 160, 200)};
    for (int i = 0; i < 2; i++) {
      _canvas.drawPixel(fl[i][0], fl[i][1] - 1, fc[i]);
      _canvas.drawPixel(fl[i][0] - 1, fl[i][1], fc[i]);
      _canvas.drawPixel(fl[i][0] + 1, fl[i][1], fc[i]);
      _canvas.drawPixel(fl[i][0], fl[i][1] + 1, fc[i]);
      _canvas.drawPixel(fl[i][0], fl[i][1], rgb565(250, 210, 60));
    }
  }
}

void Renderer::drawSparkles(bool night) {
  // Magic dust: fixed twinkling points (the rising motes competed with the
  // fireflies), keeping the newer look - per-dot rhythm and a brightness
  // ladder per blink slot: dark -> faint pixel -> lit dot -> rare star glint.
  static const int16_t pts[][2] = {
    {60, 70}, {150, 58}, {110, 90}, {30, 95}, {200, 88},
    {170, 120}, {75, 130}, {215, 130}, {45, 128},
  };
  const unsigned long ms = millis();
  const int n = night ? 9 : 5;
  for (int i = 0; i < n; i++) {
    const uint32_t seed = (uint32_t)i * 2246822519u + 0x9E3779B9u;
    const int x = pts[i][0];
    const int y = pts[i][1];
    const unsigned long ph = ms / (160 + (int)((seed >> 5) % 180));
    uint32_t h = (uint32_t)ph * 2654435761u ^ seed;
    h ^= h >> 13;
    const uint8_t st = h & 7;
    if (st < 3) continue;                       // dark beat
    if (st < 6) { _canvas.drawPixel(x, y, _p.sparkle); continue; }  // faint
    const bool glint = st == 7 && ((h >> 8) & 3) == 0;  // rare white star
    _canvas.fillCircle(x, y, 1, glint ? TFT_WHITE : _p.sparkle);    // lit
    if (glint) {                                // star glint at the peak
      _canvas.drawPixel(x - 2, y, _p.sparkle);
      _canvas.drawPixel(x + 2, y, _p.sparkle);
      _canvas.drawPixel(x, y - 2, _p.sparkle);
      _canvas.drawPixel(x, y + 2, _p.sparkle);
    }
  }
}

// Three green fireflies wandering the night scene on Lissajous paths with a
// soft pulse (bright at the peak, brief dark beats). Clear nights only -
// rain/clouds ground them (called gated from draw()).
void Renderer::drawFireflies() {
  const float t = millis() / 1000.0f;
  struct F { float cx, cy, ax, ay, px, py, ph; };
  static const F fs[3] = {
      {70, 95, 34, 18, 7.3f, 5.1f, 0.0f},
      {150, 82, 40, 22, 9.1f, 6.7f, 2.1f},
      {110, 118, 30, 14, 6.1f, 8.3f, 4.2f},
  };
  const uint16_t bright = rgb565(140, 255, 110);   // full glow
  const uint16_t faint = rgb565(26, 64, 30);       // almost melts into the night
  for (int i = 0; i < 3; i++) {
    const F& f = fs[i];
    const int x = (int)(f.cx + f.ax * sinf(t * 6.2832f / f.px + f.ph));
    const int y = (int)(f.cy + f.ay * sinf(t * 6.2832f / f.py + f.ph * 1.7f));
    // Continuous fade: brightness follows the sine (no on/off steps) -
    // lerp565 walks the whole green ramp from near-dark to neon.
    const float pulse = sinf(t * 3.0f + i * 2.0f);
    const float b = (pulse + 0.7f) / 1.7f;   // -0.7..1 -> 0..1
    if (b <= 0.02f) continue;                // fully faded out
    const uint16_t glow = lerp565(faint, bright, b);
    // short trail (where it was a moment ago) sells the flight path
    const float tp = t - 0.22f;
    const int px = (int)(f.cx + f.ax * sinf(tp * 6.2832f / f.px + f.ph));
    const int py = (int)(f.cy + f.ay * sinf(tp * 6.2832f / f.py + f.ph * 1.7f));
    _canvas.drawPixel(px, py, lerp565(faint, bright, b * 0.4f));
    _canvas.fillCircle(x, y, 1, glow);
    if (b > 0.65f) {  // halo fades in near the peak
      const uint16_t halo = lerp565(faint, bright, (b - 0.65f));
      _canvas.drawPixel(x - 2, y, halo);
      _canvas.drawPixel(x + 2, y, halo);
      _canvas.drawPixel(x, y - 2, halo);
      _canvas.drawPixel(x, y + 2, halo);
    }
  }
}

// A rare shooting star streaking the upper sky (clear nights): scheduled
// every 15-45 s, alive for ~700 ms with a fading tail.
void Renderer::drawShootingStar() {
  const unsigned long now = millis();
  if (_nextShootMs == 0) _nextShootMs = now + 8000;
  if (_shootT0 == 0 && (long)(now - _nextShootMs) >= 0) {
    _shootT0 = now;
    _ssx = 30 + (int)(esp_random() % 130);
    _ssy = 16 + (int)(esp_random() % 26);
  }
  if (_shootT0 == 0) return;
  const unsigned long p = now - _shootT0;
  if (p > 700) {  // done: schedule the next one
    _shootT0 = 0;
    _nextShootMs = now + 15000 + (esp_random() % 30000);
    return;
  }
  const int hx = _ssx + (int)(p * 0.12f);  // head position
  const int hy = _ssy + (int)(p * 0.045f);
  _canvas.drawPixel(hx, hy, TFT_WHITE);
  _canvas.drawPixel(hx - 1, hy, TFT_WHITE);
  for (int k = 2; k <= 7; k++) {           // fading tail
    if (k % 2) continue;                   // sparse = twinkly
    _canvas.drawPixel(hx - k * 2, hy - (int)(k * 0.75f), theme::STAR);
  }
}

// Two butterflies fluttering over the grass on clear days - the daytime
// counterpart of the fireflies. Wing flap = the pixels beside the body
// alternate up/out every beat.
void Renderer::drawButterflies() {
  const float t = millis() / 1000.0f;
  struct B { float cx, cy, ax, ay, px, py, ph; uint16_t c; };
  const B bs[2] = {
      {85, 104, 38, 12, 8.1f, 5.7f, 0.0f, rgb565(255, 200, 90)},   // amber
      {160, 96, 32, 15, 6.7f, 7.9f, 3.0f, rgb565(240, 240, 250)},  // white
  };
  const bool flap = (millis() / 130) & 1;
  for (int i = 0; i < 2; i++) {
    const B& b = bs[i];
    const int x = (int)(b.cx + b.ax * sinf(t * 6.2832f / b.px + b.ph));
    const int y = (int)(b.cy + b.ay * sinf(t * 6.2832f / b.py + b.ph * 1.7f));
    _canvas.drawPixel(x, y, b.c);                       // body
    if (flap) {                                         // wings out
      _canvas.drawPixel(x - 1, y, b.c);
      _canvas.drawPixel(x + 1, y, b.c);
    } else {                                            // wings up
      _canvas.drawPixel(x - 1, y - 1, b.c);
      _canvas.drawPixel(x + 1, y - 1, b.c);
    }
  }
}

// ---- Real-weather scene effects --------------------------------------------

// Blend every palette field toward an overcast gray. t = 0..255.
theme::ScenePalette Renderer::tintPalette(const theme::ScenePalette& p,
                                          uint8_t t) {
  auto mix = [&](uint16_t c) { return lerp565(c, theme::SCENE_GRAY, t / 255.0f); };
  theme::ScenePalette o = p;
  o.skyTop = mix(p.skyTop);       o.skyBottom = mix(p.skyBottom);
  o.treeFar = mix(p.treeFar);     o.ground = mix(p.ground);
  o.groundDark = mix(p.groundDark); o.treeNear = mix(p.treeNear);
  o.treeTrunk = mix(p.treeTrunk); o.cabinWall = mix(p.cabinWall);
  o.cabinRoof = mix(p.cabinRoof); o.sparkle = mix(p.sparkle);
  // cabinWindow/text/textDim/barBg keep their contrast (readability).
  return o;
}

// Up to three puffy clouds drifting slowly (millis-driven, stateless).
void Renderer::drawClouds(uint8_t n) {
  struct C { int16_t y; int16_t w; uint16_t speedMs; };  // per-cloud params
  static const C cs[3] = {{30, 46, 240}, {52, 60, 340}, {22, 38, 180}};
  if (n > 3) n = 3;
  for (int i = 0; i < n; i++) {
    const int span = SCREEN_W + cs[i].w * 2;
    const int x = (int)((millis() / cs[i].speedMs + i * 97) % span) - cs[i].w;
    const int y = cs[i].y;
    const int r = cs[i].w / 4;
    _canvas.fillCircle(x - r - 2, y + 2, r, theme::CLOUD_DARK);   // shaded side
    _canvas.fillCircle(x + r + 2, y + 2, r, theme::CLOUD);
    _canvas.fillCircle(x, y - r / 2, r + 3, theme::CLOUD);        // top puff
    _canvas.fillRect(x - r - 2, y + 2, (r + 2) * 2, r, theme::CLOUD);  // base
  }
}

// Falling rain, scaled by intensity: 0 = drizzle (few thin short drops),
// 1 = steady rain, 2 = downpour (dense, long, fast). Freezing rain goes icy
// pale with little crystals glinting near the ground.
void Renderer::drawRain(uint8_t intensity, bool freezing) {
  const unsigned long t = millis();
  static const uint8_t N[3] = {8, 16, 26};
  static const uint8_t SPD[3] = {8, 5, 4};   // ms per px (lower = faster)
  static const uint8_t LEN[3] = {3, 5, 7};
  const uint16_t c = freezing ? rgb565(205, 230, 250) : theme::RAINDROP;
  for (int i = 0; i < N[intensity]; i++) {
    const int x = (i * 61 + 13) % SCREEN_W;
    const int y = (int)((t / SPD[intensity] + i * 977) % (GROUND_Y + 40));
    _canvas.drawLine(x, y, x - 1, y + LEN[intensity], c);
  }
  if (freezing) {  // ice crystals twinkling at ground level
    const unsigned long ph = t / 300;
    for (int i = 0; i < 6; i++) {
      if ((ph + i) % 3 == 0) continue;
      _canvas.fillCircle((i * 41 + 17) % SCREEN_W, GROUND_Y + 4 + (i * 7) % 18,
                         1, TFT_WHITE);
    }
  }
}

// Snow: white flakes with a gentle wobble. grains = tiny fast specks (WMO 77);
// fast = snow showers. Intensity scales the flake count.
void Renderer::drawSnow(uint8_t intensity, bool grains, bool fast) {
  const unsigned long t = millis();
  static const uint8_t N[3] = {10, 14, 22};
  const int spd = grains ? 8 : (fast ? 10 : 18);  // ms per px
  for (int i = 0; i < N[intensity]; i++) {
    const int y = (int)((t / spd + i * 977) % (GROUND_Y + 40));
    const int wob = grains ? 0 : (int)(3.0f * sinf(t / 900.0f + i * 1.7f));
    const int x = ((i * 67 + 21) % SCREEN_W) + wob;
    _canvas.fillCircle(x, y, (!grains && i % 3 == 0) ? 2 : 1, TFT_WHITE);
  }
}

// Hail: white pellets falling fast in front of everything.
void Renderer::drawHail() {
  const unsigned long t = millis();
  for (int i = 0; i < 10; i++) {
    const int x = (i * 83 + 29) % SCREEN_W;
    const int y = (int)((t / 3 + i * 977) % (GROUND_Y + 50));
    _canvas.fillCircle(x, y, 2, TFT_WHITE);
    _canvas.drawPixel(x - 1, y - 3, theme::CLOUD);  // motion hint
  }
}

// A jagged lightning bolt from the clouds to the treeline. The seed keeps the
// shape stable for the strike's ~6 frames; each strike lands somewhere new.
void Renderer::drawLightning() {
  uint32_t s = _boltSeed;
  auto rnd = [&s]() { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7FFF; };
  int x = _boltX, y = 14;
  const uint16_t core = TFT_WHITE;
  while (y < GROUND_Y - 6) {
    const int ny = y + 12 + (int)(rnd() % 10);
    const int nx = x + (int)(rnd() % 25) - 12;
    _canvas.drawLine(x, y, nx, ny, theme::BOLT);
    _canvas.drawLine(x + 1, y, nx + 1, ny, theme::BOLT);
    _canvas.drawLine(x, y + 1, nx, ny + 1, core);
    // occasional side branch
    if (rnd() % 3 == 0) {
      _canvas.drawLine(nx, ny, nx + ((rnd() % 2) ? 10 : -10), ny + 8, theme::BOLT);
    }
    x = nx; y = ny;
  }
}

// Fog: real translucent mist. RGB565 has no alpha, but the canvas is ours -
// blend each pixel toward the fog color directly in the buffer (big-endian,
// hence the byte swaps). Three soft bands with vertical falloff and a slow
// horizontal waviness so the mist rolls instead of looking like stripes.
void Renderer::drawFog() {
  uint16_t* buf = (uint16_t*)_canvas.getBuffer();
  if (!buf) return;
  const float t = millis() / 1000.0f;
  struct Band { int16_t cy, half; float speed; };
  static const Band bands[3] = {{92, 9, 0.9f}, {114, 11, -0.6f}, {138, 13, 0.4f}};
  float wav[SCREEN_W];
  for (int b = 0; b < 3; b++) {
    // Per-column strength wave, computed once per band (sinf is pricey).
    for (int x = 0; x < SCREEN_W; x++)
      wav[x] = 0.7f + 0.3f * sinf(x * 0.05f + t * bands[b].speed + b * 2.1f);
    for (int dy = -bands[b].half; dy <= bands[b].half; dy++) {
      const int y = bands[b].cy + dy;
      if (y < 0 || y >= SCREEN_H) continue;
      const float fall = 1.0f - fabsf((float)dy) / (bands[b].half + 1.0f);
      uint16_t* row = &buf[y * SCREEN_W];
      for (int x = 0; x < SCREEN_W; x++) {
        const float a = 0.55f * fall * wav[x];
        const uint16_t c = __builtin_bswap16(row[x]);
        row[x] = __builtin_bswap16(lerp565(c, theme::CLOUD, a));
      }
    }
  }
}

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

// Checkered dance floor over the grass strip, drawn UNDER the pet so Leon
// dances on it. Tiles cycle through party colors in a rolling diagonal
// pattern, with a dark seam grid so it reads as tiles.
void Renderer::drawDiscoFloor() {
  const uint32_t t = millis();
  // Full-saturation neon palette.
  static constexpr uint16_t TILES[] = {
      0xF81F /*magenta*/, 0x07FF /*cyan*/,  0xFC40 /*orange*/,
      0xC81F /*violet*/,  0x07E0 /*green*/, 0xF800 /*red*/,
      0x3A9F /*blue*/,    0xFFE0 /*yellow*/};
  // Beveled tile with an occasional white strobe-flash. Colors are truly
  // random per tile: each tile runs its OWN change clock (phase-offset by a
  // per-tile hash) and each change picks a hashed random palette color - no
  // sliding diagonal pattern, tiles pop independently like a real club floor.
  auto tile = [&](int x, int y, int w, int h, int col, int row) {
    const uint32_t seed = (uint32_t)(col * 977 + row * 541);
    const uint32_t roll = (t + seed * 97u) % 0xFFFFFFFFu / 300u;  // own clock
    uint32_t hsh = (roll * 2654435761u) ^ (seed * 0x9E3779B9u);
    hsh ^= hsh >> 13;
    hsh *= 0x5bd1e995u;
    hsh ^= hsh >> 15;
    const bool flash = (hsh % 13) == 0;
    const uint16_t c = flash ? 0xFFFF : TILES[(hsh >> 4) % 8];
    _canvas.fillRect(x, y, w, h, c);
    // Gloss bevel: light top/left, dark bottom/right.
    _canvas.drawFastHLine(x, y, w, flash ? 0xFFFF : 0xC618);
    _canvas.drawFastVLine(x, y, h, flash ? 0xFFFF : 0xC618);
    _canvas.drawFastHLine(x, y + h - 1, w, 0x2104);
    _canvas.drawFastVLine(x + w - 1, y, h, 0x2104);
  };

  // A dance-floor STRIP over the grass under the pet (full-grass coverage
  // was tried and looked too busy) - the rest of the grass stays grass.
  const int y0 = 112, tileH = 11, tileW = 20;
  for (int row = 0; row < 2; row++)
    for (int col = 0; col < 12; col++)
      tile(col * tileW, y0 + row * tileH, tileW, tileH, col, row);

  // Two speaker cabinets flanking the stage (drawn under the pet: Leon can
  // strut in front of them). The woofer ring thumps to a beat envelope.
  for (int side = 0; side < 2; side++) {
    const int sx = side ? 212 : 2;  // cabinet left edge
    const int sy = 82;              // cabinet top; bottom lands on the floor
    _canvas.fillRect(sx, sy, 26, 30, _canvas.color565(22, 22, 28));
    _canvas.drawRect(sx, sy, 26, 30, _canvas.color565(70, 70, 82));
    const int cxs = sx + 13;
    // Tweeter.
    _canvas.fillCircle(cxs, sy + 7, 3, _canvas.color565(10, 10, 14));
    _canvas.drawCircle(cxs, sy + 7, 3, _canvas.color565(120, 120, 132));
    // Woofer: radius pumps on the positive half of the beat (offset phases
    // so the two boxes alternate).
    const float beat = sinf(t / 140.0f + side * 1.6f);
    const int wr = 7 + (int)(2.5f * (beat > 0 ? beat : 0.0f));
    _canvas.fillCircle(cxs, sy + 20, wr, _canvas.color565(10, 10, 14));
    _canvas.drawCircle(cxs, sy + 20, wr, _canvas.color565(155, 155, 170));
    _canvas.drawCircle(cxs, sy + 20, wr - 3 > 2 ? wr - 3 : 2,
                       _canvas.color565(80, 80, 92));
    _canvas.drawPixel(cxs, sy + 20, 0xE71C);  // dust cap glint
  }

  // Smoke machines on each side of the floor. Part of the stage set (drawn
  // under the HUD so the edge pull-handles/bars/buttons stay on top, and
  // under the pet so Leon dances in front of the fog).
  // Puff "translucency" is dithering: only a checkerboard of pixels is
  // drawn, sparser as the puff ages, so the scene shows through.
  for (int m = 0; m < 2; m++) {
    const int dir = m ? -1 : 1;             // left blows right, right blows left
    const int mx = m ? 224 : 16, my = 124;
    for (int p = 0; p < 4; p++) {
      const uint32_t age = (t / 18 + p * 55 + m * 27) % 220;  // staggered
      const float a = age / 220.0f;         // 0 fresh .. 1 dissolved
      const int px = mx + dir * (8 + (int)(a * 72));
      const int py = my - 10 - (int)(a * 48) + (int)(4 * sinf(t / 300.0f + p * 2.1f + m));
      const int pr = 3 + (int)(a * 8);
      const int step = a < 0.5f ? 2 : 3;    // fresh = denser
      const uint16_t pc = a < 0.4f ? 0xE71C : 0xC618;
      for (int dy = -pr; dy <= pr; dy++) {
        for (int dx = -pr; dx <= pr; dx++) {
          if (dx * dx + dy * dy > pr * pr) continue;
          if ((dx + dy + (int)(t / 130)) % step) continue;  // dither + shimmer
          _canvas.drawPixel(px + dx, py + dy, pc);
        }
      }
    }
    // Machine box + nozzle tilted toward the center, sitting on the floor.
    _canvas.fillRect(mx - 9, my - 4, 18, 9, _canvas.color565(48, 48, 56));
    _canvas.fillRect(m ? mx - 10 : mx + 5, my - 7, 5, 4, _canvas.color565(72, 72, 84));
    _canvas.drawPixel(m ? mx - 9 : mx + 9, my - 6, 0xFFFF);  // status LED
  }
}

// Overlay drawn while media plays. kind 1 = music: disco ball + corner
// lasers over the scene. kind 2 = speech: an Alexa-style cyan ring
// sweeps around the round display edge while the assistant talks.
void Renderer::drawMediaFx(uint8_t kind) {
  const uint32_t t = millis();
  if (kind == 1) {
    // --- disco ball + corner lasers ---
    const int bx = 120, by = 52, br = 13;
    static constexpr uint16_t LASER[] = {0xF81F /*magenta*/, 0x07E0 /*green*/,
                                         0x07FF /*cyan*/, 0xFC00 /*orange*/};
    // Two emitters up on the left and right edges firing SHORT PULSES: each
    // stream picks a random inward-down direction per shot and a bright dash
    // travels from the emitter to the floor, then vanishes. Some slots skip,
    // so the timing feels random - club tracer shots, not solid bars.
    struct { int x, y; float dir; } emit[2] = {{26, 44, 1.0f}, {214, 44, -1.0f}};
    for (int e = 0; e < 2; e++) {
      for (int i = 0; i < 3; i++) {          // 3 shot streams per emitter
        const int bi = e * 3 + i;
        const uint32_t period = 260 + bi * 37;  // desynced stream cadences
        const uint32_t slot = t / period;
        uint32_t h = slot * 2654435761u + (uint32_t)bi * 40503u;  // slot hash
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        if (h % 3 == 0) continue;            // this slot doesn't fire
        // Random direction for this shot (inward + down from the emitter).
        const float th = (0.25f + 1.05f * ((h >> 8) % 100) / 100.0f) * emit[e].dir;
        const float sx = sinf(th), cy = cosf(th);
        float len = 115.0f;
        if (emit[e].y + cy * len > 130.0f) len = (130.0f - emit[e].y) / cy;
        // Pulse head travels 0..len over the slot; 14px tail behind it.
        const float prog = (float)(t % period) / (float)period;
        const float headD = prog * len;
        const float tailD = headD - 14.0f > 0 ? headD - 14.0f : 0;
        const int hx = emit[e].x + (int)(sx * headD), hy = emit[e].y + (int)(cy * headD);
        const int tx = emit[e].x + (int)(sx * tailD), ty = emit[e].y + (int)(cy * tailD);
        const uint16_t col = LASER[h % 4];
        _canvas.drawLine(tx - 1, ty, hx - 1, hy, col);   // neon halo
        _canvas.drawLine(tx + 1, ty, hx + 1, hy, col);
        _canvas.drawLine(tx, ty, hx, hy, 0xFFFF);        // white-hot core
        if (prog > 0.86f) {                              // impact flash
          _canvas.fillCircle(hx, hy, 2, col);
          _canvas.drawPixel(hx, hy, 0xFFFF);
        }
      }
      // The emitter box itself.
      _canvas.fillRect(emit[e].x - 3, emit[e].y - 3, 7, 6, _canvas.color565(60, 60, 70));
      _canvas.drawPixel(emit[e].x, emit[e].y, 0xFFFF);
    }
    // Cord + ball.
    _canvas.drawFastVLine(bx, 0, by - br, _canvas.color565(90, 90, 100));
    _canvas.fillCircle(bx, by, br, _canvas.color565(148, 150, 162));
    // Mirror facets: dark grid dots sliding sideways = spinning illusion.
    const int slide = (t / 90) % 4;
    for (int fy = -br + 2; fy <= br - 2; fy += 4) {
      const int half = (int)sqrtf((float)(br * br - fy * fy)) - 1;
      for (int fx = -half + slide; fx <= half; fx += 4) {
        _canvas.drawPixel(bx + fx, by + fy, _canvas.color565(96, 98, 110));
      }
    }
    // Glint + twinkles (sparkle positions hop with time).
    _canvas.fillCircle(bx - 4, by - 5, 2, 0xFFFF);
    for (int k = 0; k < 3; k++) {
      const uint32_t s = t / 160 + k * 7919;  // cheap hash per twinkle
      const int tx = bx - br + 2 + (int)(s * 31 % (2 * br - 4));
      const int ty = by - br + 2 + (int)(s * 17 % (2 * br - 4));
      const int dx = tx - bx, dy = ty - by;
      if (dx * dx + dy * dy <= (br - 2) * (br - 2)) {
        _canvas.drawPixel(tx, ty, 0xFFFF);
        _canvas.drawPixel(tx + 1, ty, LASER[k % 4]);
      }
    }
  }
}

// Assistant voice feedback ring, flush with the round glass edge (outer radius
// 120). Three distinct looks so each phase reads at a glance:
//   1 listening - breathing cyan ring + a comet with a long gradient tail
//   2 thinking  - two amber comets chasing each other with fading tails
//   3 speaking  - organic equalizer: bars with per-bar rhythm + hot tips
void Renderer::drawVoiceRing(uint8_t state) {
  const uint32_t t = millis();
  const int cx = 120, cy = 120;
  // The glass is centered on 119.5, so a band ending exactly at r=120 from
  // integer (120,120) leaves a half-pixel sliver of scene visible on one side.
  // Overdraw well past the edge (clipped by the canvas): guaranteed flush.
  const int r1 = 126;       // past the glass edge (clipped)
  const int r0 = 106;       // band thickness ~14px visible
  if (state == 1) {
    // LISTENING: the whole ring breathes softly (I'm open), while a bright
    // comet sweeps around with an 8-step gradient tail melting into the base.
    const float br = 0.5f + 0.5f * sinf(t / 480.0f);
    const uint8_t bg = (uint8_t)(30 + 45 * br), bb = (uint8_t)(55 + 70 * br);
    _canvas.fillArc(cx, cy, r0, r1, 0, 360, _canvas.color565(0, bg, bb));
    const int a = (int)((t / 6) % 360);  // comet head angle
    for (int i = 7; i >= 0; i--) {       // tail: oldest (dim) -> head (hot)
      const float f = 1.0f - i / 8.0f;
      const int s0 = (a - (i + 1) * 13 + 720) % 360;
      _canvas.fillArc(cx, cy, r0, r1, s0, (s0 + 14) % 360,
                      _canvas.color565((uint8_t)(20 * f),
                                       (uint8_t)(bg + (200 - bg) * f),
                                       (uint8_t)(bb + (255 - bb) * f)));
    }
    _canvas.fillArc(cx, cy, r0 + 2, r1 - 2, a, (a + 8) % 360, 0xFFFF);  // hot core
  } else if (state == 2) {
    // THINKING: two amber comets 180 degrees apart orbiting fast over a warm
    // breathing amber base (bright base like listening - no dark background).
    const float br = 0.5f + 0.5f * sinf(t / 420.0f);
    const uint8_t bgr = (uint8_t)(85 + 55 * br), bgg = (uint8_t)(46 + 32 * br);
    _canvas.fillArc(cx, cy, r0, r1, 0, 360, _canvas.color565(bgr, bgg, 0));
    const int a = (int)((t / 3) % 360);
    for (int c = 0; c < 2; c++) {
      const int head = (a + c * 180) % 360;
      for (int i = 5; i >= 0; i--) {
        const float f = 1.0f - i / 6.0f;
        const int s0 = (head - (i + 1) * 11 + 720) % 360;
        _canvas.fillArc(cx, cy, r0, r1, s0, (s0 + 12) % 360,
                        _canvas.color565((uint8_t)(bgr + (255 - bgr) * f),
                                         (uint8_t)(bgg + (205 - bgg) * f),
                                         (uint8_t)(110 * f * f)));
      }
      _canvas.fillArc(cx, cy, r0 + 2, r1, head, (head + 7) % 360,
                      _canvas.color565(255, 248, 210));  // hot core
    }
  } else {
    // SPEAKING: smooth waves of light flowing around the ring - two wave
    // trains traveling in opposite directions (3 crests one way, 5 the other)
    // whose interference makes the ring shimmer organically, like sound
    // rippling along the rim. Crests go white-hot. No bars, no hard edges.
    const int SEG = 8;  // degrees per slice (45 slices, smooth gradient)
    for (int s = 0; s < 360; s += SEG) {
      const float rad = (s + SEG * 0.5f) * 0.017453f;
      float w = 0.62f
              + 0.26f * sinf(rad * 3.0f - t / 150.0f)   // 3 crests, clockwise
              + 0.22f * sinf(rad * 5.0f + t / 95.0f);   // 5 crests, counter
      // High floor: troughs stay clearly cyan (bright base, no dark patches).
      if (w < 0.34f) w = 0.34f;
      if (w > 1.0f) w = 1.0f;
      // Cyan body; crests (w>0.78) blend toward white-hot.
      const float hot = w > 0.78f ? (w - 0.78f) * 4.5f : 0.0f;
      const uint8_t rr = (uint8_t)(190 * hot);
      const uint8_t g  = (uint8_t)(60 + 195 * w);
      const uint8_t b  = (uint8_t)(90 + 165 * w);
      _canvas.fillArc(cx, cy, r0, r1, s, s + SEG, _canvas.color565(rr, g, b));
    }
  }
}
