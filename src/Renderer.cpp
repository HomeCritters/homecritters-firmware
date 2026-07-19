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

// --- Screen lifecycle -------------------------------------------------------
// Every full-screen draw path starts with beginScreen (background fill) and
// MUST end with endScreen on every return path: it pushes the canvas to the
// LCD. Forgetting the push freezes the physical display on the last frame
// while screenshots (which read the CANVAS, not the LCD) keep showing the new
// screen looking perfect - a bug class this codebase already shipped once
// (drawHaPanel). Route every push through here; never call pushSprite direct.
void Renderer::beginScreen(uint16_t bg) { _canvas.fillScreen(bg); }
void Renderer::endScreen() { endScreen(); }

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
void Renderer::drawLightIcon(int cx, int cy, bool on) {
  const uint16_t base = rgb565(150, 150, 160);
  if (on) {
    // Lit: amber glass + highlight + short rays around it.
    _canvas.fillCircle(cx, cy - 4, 9, menu::IC_SUN);       // glass
    _canvas.fillCircle(cx - 3, cy - 7, 3, menu::IC_SUN2);  // highlight
    for (int a = 0; a < 8; a++) {                          // rays
      const float t = a * 3.14159f / 4.0f;
      const int x0 = cx + (int)(11 * cosf(t)), y0 = cy - 4 + (int)(11 * sinf(t));
      const int x1 = cx + (int)(14 * cosf(t)), y1 = cy - 4 + (int)(14 * sinf(t));
      _canvas.drawLine(x0, y0, x1, y1, menu::IC_SUN);
    }
  } else {
    // Off: dark gray glass, outlined, no highlight, no rays.
    _canvas.fillCircle(cx, cy - 4, 9, rgb565(70, 72, 84));
    _canvas.drawCircle(cx, cy - 4, 9, rgb565(120, 124, 140));
  }
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

// Padlock: gold body + gray shackle (Seguranca tile).
void Renderer::drawLockIcon(int cx, int cy) {
  const uint16_t body = rgb565(235, 180, 50), dark = rgb565(150, 110, 20);
  _canvas.fillArc(cx, cy - 3, 5, 8, 180, 360, rgb565(165, 165, 175));  // shackle
  _canvas.fillRoundRect(cx - 9, cy - 3, 18, 15, 3, body);              // body
  _canvas.drawRoundRect(cx - 9, cy - 3, 18, 15, 3, dark);
  _canvas.fillCircle(cx, cy + 3, 2, dark);                             // keyhole
  _canvas.drawFastVLine(cx, cy + 4, 4, dark);
}

// Connections (Conexoes tile): a monitor + a phone, universal "devices"
// glyph. The monitor stand and base are centered UNDER the screen.
void Renderer::drawHouseIcon(int cx, int cy) {
  const uint16_t body = rgb565(210, 218, 230), scr = rgb565(70, 160, 235),
                 dark = rgb565(70, 82, 100);
  const int mx = cx - 5;  // monitor center (leaves room for the phone at right)
  _canvas.fillRoundRect(mx - 10, cy - 10, 20, 15, 2, body);   // bezel
  _canvas.drawRoundRect(mx - 10, cy - 10, 20, 15, 2, dark);
  _canvas.fillRect(mx - 8, cy - 8, 16, 11, scr);              // screen
  _canvas.fillRect(mx - 2, cy + 5, 4, 3, dark);               // stand (centered)
  _canvas.fillRect(mx - 6, cy + 8, 12, 2, dark);              // base (centered)
  // Phone at the right, slightly lower.
  _canvas.fillRoundRect(cx + 7, cy - 4, 9, 15, 2, dark);
  _canvas.fillRect(cx + 9, cy - 1, 5, 9, scr);
  _canvas.fillCircle(cx + 11, cy + 9, 1, body);               // home button
}

// Key (Parear tile): horizontal gold key - bow ring left, straight shaft
// right with two teeth. Reads cleanly at tile size.
void Renderer::drawKeyIcon(int cx, int cy) {
  const uint16_t gold = rgb565(240, 185, 55), dark = rgb565(165, 120, 25);
  _canvas.fillCircle(cx - 8, cy, 7, gold);            // bow (ring)
  _canvas.drawCircle(cx - 8, cy, 7, dark);
  _canvas.fillCircle(cx - 8, cy, 3, menu::CELL_BG);   // ring hole
  _canvas.fillRect(cx - 1, cy - 2, 16, 4, gold);      // shaft
  _canvas.drawFastHLine(cx - 1, cy - 2, 16, dark);    // shaft top edge
  _canvas.fillRect(cx + 11, cy + 2, 3, 6, gold);      // tooth (tip)
  _canvas.fillRect(cx + 5, cy + 2, 3, 4, gold);       // tooth (mid)
}

// Browser window (Portal tile): white page, blue title bar with dots and
// gray content lines - much more readable than the old mini-QR.
void Renderer::drawQrGlyph(int cx, int cy) {
  const uint16_t bar = rgb565(90, 125, 210), line = rgb565(185, 195, 212);
  _canvas.fillRoundRect(cx - 13, cy - 11, 26, 22, 3, TFT_WHITE);  // page
  _canvas.fillRoundRect(cx - 13, cy - 11, 26, 8, 3, bar);         // title bar
  _canvas.fillRect(cx - 13, cy - 6, 26, 3, bar);                  // square bar bottom
  _canvas.drawRoundRect(cx - 13, cy - 11, 26, 22, 3, bar);
  _canvas.fillCircle(cx - 9, cy - 7, 1, TFT_WHITE);               // window dots
  _canvas.fillCircle(cx - 5, cy - 7, 1, TFT_WHITE);
  _canvas.fillRect(cx - 9, cy - 1, 18, 2, line);                  // content lines
  _canvas.fillRect(cx - 9, cy + 3, 13, 2, line);
  _canvas.fillRect(cx - 9, cy + 7, 16, 2, line);
}

// One grid cell: light tile, colored icon on top, label under it.
void Renderer::drawGridCell(int x, int y, const char* label, char icon) {
  _canvas.fillRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, MENU_CELL_W, MENU_CELL_H, 12, BTN_BORDER);
  const int cx = x + MENU_CELL_W / 2, iy = y + 26;
  switch (icon) {
    case 'a': drawAudioIcon(cx, iy, menu::CELL_BG); break;
    case 'l': drawLightIcon(cx, iy);                break;
    case 'w': drawWifiIcon(cx, iy);                 break;
    case 's': drawLockIcon(cx, iy);                 break;
    case 'd': drawHouseIcon(cx, iy);                break;  // devices glyph
    case 'k': drawKeyIcon(cx, iy);                  break;
    case 'q': drawQrGlyph(cx, iy);                  break;
  }
  _canvas.setTextColor(menu::CELL_LABEL);
  _canvas.setTextSize(1);
  _canvas.setCursor(cx - _canvas.textWidth(label) / 2, y + MENU_CELL_H - 15);
  _canvas.print(label);
}

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

// ---- Weather forecast screen -----------------------------------------------

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

void Renderer::drawMenu(ui::MenuPage page, int volume, int ledBright,
                        int batteryPct, bool wifiOn, const char* ip) {
  beginScreen(menu::BG);  // full-screen dark purple background
  if (page == PAGE_AUDIO)       drawMenuAudio(volume);
  else if (page == PAGE_LIGHT)  drawMenuLight(ledBright);
  else if (page == PAGE_CONN)   drawMenuConn(wifiOn, ip);
  else if (page == PAGE_QR)     drawMenuQr(wifiOn, ip);
  else if (page == PAGE_SEC)    drawMenuSec();
  else if (page == PAGE_SEC_HA) drawMenuSecHa();
  else                          drawMenuMain(batteryPct, wifiOn, ip);
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
  drawGridCell(MENU_COL_L, MENU_ROW_2, "Conexao", 'w');
  drawGridCell(MENU_COL_R, MENU_ROW_2, "Seguranca", 's');

  drawLeftHandle();  // pull (or tap) the left tab to close the menu
}

// Conexao: WiFi setup + Portal (QR). Shows the connection status up top.
void Renderer::drawMenuConn(bool wifiOn, const char* ip) {
  char status[48];
  if (wifiOn && ip && ip[0]) snprintf(status, sizeof(status), "Conectado: %s", ip);
  else strlcpy(status, "Sem WiFi", sizeof(status));
  drawPageHeader("Conexao", status);

  drawGridCell(MENU_COL_L, MENU_SUB_ROW, "WiFi", 'w');
  drawGridCell(MENU_COL_R, MENU_SUB_ROW, "Portal", 'q');
  drawLeftHandle();
}

// Centered page title + subtitle at a y that clears the round bezel: a
// full-width title at the very top (y=14) gets clipped where the circle
// narrows, so titles start lower where the chord is wide enough.
void Renderer::drawPageHeader(const char* title, const char* sub) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 28);
  _canvas.print(title);
  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sub) / 2, 52);
  _canvas.print(sub);
}

// Seguranca: Dispositivos (paired clients) + Parear (PIN).
void Renderer::drawMenuSec() {
  drawPageHeader("Seguranca", "Pareamento e acesso");
  drawGridCell(MENU_COL_L, MENU_SUB_ROW, "Conexoes", 'd');
  drawGridCell(MENU_COL_R, MENU_SUB_ROW, "Parear", 'k');
  drawLeftHandle();
}

// Seguranca > Dispositivos: who is paired right now (authed clients, IP list).
void Renderer::drawMenuSecHa() {
  drawPageHeader("Conexoes", "Conectados agora (HA / portal)");

  // One centered line per client IP ('\n'-separated summary from main).
  _canvas.setTextColor(TFT_WHITE);
  int y = 80;
  const char* p = _clientsInfo;
  while (*p && y < 160) {
    const char* nl = strchr(p, '\n');
    const int len = nl ? (int)(nl - p) : (int)strlen(p);
    char line[40];
    snprintf(line, sizeof(line), "%.*s", len, p);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(line) / 2, y);
    _canvas.print(line);
    y += 16;
    p += len + (nl ? 1 : 0);
    if (!nl) break;
  }

  // Revoke-all button (two-tap confirm): rotates the token and kicks every
  // paired client - they all have to PIN-pair again.
  const uint16_t red = _revokeArmed ? rgb565(230, 60, 50) : rgb565(120, 35, 35);
  _canvas.fillRoundRect(REVOKE_X, REVOKE_Y, REVOKE_W, REVOKE_H, 8, red);
  _canvas.drawRoundRect(REVOKE_X, REVOKE_Y, REVOKE_W, REVOKE_H, 8, rgb565(255, 120, 110));
  const char* lbl = _revokeArmed ? "Confirma?" : "Revogar acesso";
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(lbl) / 2, REVOKE_Y + 11);
  _canvas.print(lbl);
  drawLeftHandle();
}

// Seguranca > Senha: THE pairing token - the only place it is ever shown
// (screen = physical access = trusted). Portal and HA ask for it once.
// Full-screen pairing overlay: pops automatically when a client asks to pair
// (or via Seguranca > Parear). Six digit boxes, TV-pairing style.
void Renderer::drawPairingOverlay() {
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Pareamento";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 26);
  _canvas.print(title);

  // Six digit boxes centered: 30px boxes with 4px gaps (6*30+5*4 = 200px).
  const int bw = 30, bh = 40, gap = 4;
  const int x0 = CENTER_X - (6 * bw + 5 * gap) / 2;
  const int y0 = 92;
  _canvas.setTextSize(3);
  for (int i = 0; i < 6 && _pairPin[i]; i++) {
    const int x = x0 + i * (bw + gap);
    _canvas.fillRoundRect(x, y0, bw, bh, 6, menu::CELL_BG);
    _canvas.drawRoundRect(x, y0, bw, bh, 6, menu::AP_NAME);
    _canvas.setTextColor(menu::BG);
    // Optical centering: the 6x8 font carries a 1px trailing gap baked into
    // the glyph (15px of ink in an 18px cell at size 3), so a "mathematical"
    // center sits visibly left. +2 rebalances; same idea vertically (21px of
    // ink in a 24px cell).
    _canvas.setCursor(x + (bw - 18) / 2 + 2, y0 + (bh - 21) / 2);
    _canvas.print(_pairPin[i]);
  }

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* l1 = "Digite este codigo no app";
  const char* l2 = "para conectar (90s).";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 152);
  _canvas.print(l1);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 164);
  _canvas.print(l2);

  // "Voltar" button - closes the pairing window.
  _canvas.fillRoundRect(PAIR_CANCEL_X, PAIR_CANCEL_Y, PAIR_CANCEL_W, PAIR_CANCEL_H, 8,
                        menu::CELL_BG);
  _canvas.drawRoundRect(PAIR_CANCEL_X, PAIR_CANCEL_Y, PAIR_CANCEL_W, PAIR_CANCEL_H, 8,
                        rgb565(200, 90, 90));
  _canvas.setTextSize(2);
  _canvas.setTextColor(rgb565(200, 60, 60));
  const char* lbl = "Voltar";
  // Optical centering: ink is textWidth minus the trailing 2px gap (size 2).
  _canvas.setCursor(CENTER_X - (_canvas.textWidth(lbl) - 2) / 2,
                    PAIR_CANCEL_Y + (PAIR_CANCEL_H - 14) / 2);
  _canvas.print(lbl);
}

// QR detail page: the code (large) + what it is + how to use it + the URLs.
void Renderer::drawMenuQr(bool wifiOn, const char* ip) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Portal";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 8);
  _canvas.print(title);

  _canvas.setTextSize(1);
  _canvas.setTextColor(menu::TEXT_DIM);
  if (wifiOn && ip && ip[0]) {
    char qrUrl[40];
    snprintf(qrUrl, sizeof(qrUrl), "http://%s/", ip);
    drawQr(qrUrl, 26);  // centered, ~70px
    const char* l1 = "Aponte a camera do celular";
    const char* l2 = "para abrir o painel.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l1) / 2, 108);
    _canvas.print(l1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(l2) / 2, 122);
    _canvas.print(l2);
    char urlIp[40];
    snprintf(urlIp, sizeof(urlIp), "http://%s", ip);
    _canvas.setTextColor(menu::AP_NAME);
    _canvas.setCursor(CENTER_X - _canvas.textWidth("http://critter.local") / 2, 146);
    _canvas.print("http://critter.local");
    _canvas.setCursor(CENTER_X - _canvas.textWidth(urlIp) / 2, 160);
    _canvas.print(urlIp);
  } else {
    const char* m1 = "Conecte o WiFi primeiro";
    const char* m2 = "para gerar o QR do painel.";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m1) / 2, 96);
    _canvas.print(m1);
    _canvas.setCursor(CENTER_X - _canvas.textWidth(m2) / 2, 110);
    _canvas.print(m2);
  }

  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawMenuAudio(int volume) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Audio";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("Volume", volume, MENU_VOL_MINUS, MENU_VOL_PLUS);
  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawMenuLight(int ledBright) {
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Luz";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawStepper("LED", ledBright, MENU_LED_MINUS, MENU_LED_PLUS);
  drawStepper("Tela", _scrBright, MENU_SCR_MINUS, MENU_SCR_PLUS);
  drawLeftHandle();  // pull (or tap) the left tab to go back
}

void Renderer::drawWifiConfig(const char* apName) {
  beginScreen(menu::BG);
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

  drawLeftHandle();  // pull (or tap) the left tab to exit config
  endScreen();
}

// ------------------- Games -------------------

// A game tile: light square with a colored icon on top and a label under it.
void Renderer::drawGameTile(int x, int y, const char* label, char icon, uint16_t iconColor) {
  _canvas.fillRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, menu::CELL_BG);
  _canvas.drawRoundRect(x, y, GAME_TILE_W, GAME_TILE_H, 12, BTN_BORDER);
  const int cx = x + GAME_TILE_W / 2, cy = y + 26;  // config-cell icon center
  if (icon == 's') {  // Genius: the 4-color wheel
    _canvas.fillArc(cx, cy, 16, 6, 182, 268, rgb565(60, 200, 80));   // TL green
    _canvas.fillArc(cx, cy, 16, 6, 272, 358, rgb565(230, 60, 50));   // TR red
    _canvas.fillArc(cx, cy, 16, 6, 92, 178, rgb565(235, 210, 50));   // BL yellow
    _canvas.fillArc(cx, cy, 16, 6, 2, 88, rgb565(70, 120, 235));     // BR blue
  } else if (icon == 'j') {  // Doodle Jump: the doodler bouncing on a platform
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
  beginScreen(menu::BG);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  const char* title = "Jogos";
  _canvas.setCursor(CENTER_X - _canvas.textWidth(title) / 2, 14);
  _canvas.print(title);

  drawGameTile(GAME_COL_L, GAME_ROW_1, "Jump!", 'j', menu::IC_DOODLE);
  drawGameTile(GAME_COL_R, GAME_ROW_1, "Bolinha", 'b', menu::IC_BALL);
  drawGameTile(GAME_COL_C, GAME_ROW_2, "Genius", 's', 0);

  drawLeftHandle();  // pull (or tap) the left tab to go back to the pet
  endScreen();
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
  beginScreen(rgb565(150, 205, 235));  // light sky

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

  endScreen();
}

// The game ferret with a mode-appropriate animation, centered at (cx,cy).
// zoom>1 nearest-scales the 40px sprite (used at 2x in Bolinha).
void Renderer::drawGameFerret(int cx, int cy, int mode, bool faceLeft, float zoom) {
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
  if (zoom <= 1.01f) {
    _canvas.pushImage(cx - FERRET_G_FRAME_W / 2, cy - FERRET_G_FRAME_H / 2,
                      FERRET_G_FRAME_W, FERRET_G_FRAME_H, fr,
                      (uint16_t)FERRET_G_TRANSPARENT_KEY);
  } else {
    _canvas.pushImageRotateZoom(cx, cy, FERRET_G_FRAME_W / 2.0f, FERRET_G_FRAME_H / 2.0f,
                                0.0f, zoom, zoom,
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

  drawLeftHandle();  // pull right (or tap) to exit

  if (game.ready()) {
    _canvas.setTextSize(1);
    const char* hint = "arraste p/ lancar";
    const int hw = _canvas.textWidth(hint);
    _canvas.fillRoundRect(CENTER_X - hw / 2 - 5, 201, hw + 10, 13, 4, menu::CLOCK_BG);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(CENTER_X - hw / 2, 204);
    _canvas.print(hint);
  }

  endScreen();
}

void Renderer::drawSimon(SimonGame& game) {
  beginScreen(menu::BG);

  // Four diagonal quadrant pads on a ring (classic Genius layout).
  // Index: 0=TL green, 1=TR red, 2=BL yellow, 3=BR blue.
  static const uint16_t DIM[4] = {
    rgb565(22, 92, 38), rgb565(115, 28, 26), rgb565(120, 102, 18), rgb565(24, 48, 115)};
  static const uint16_t LIT[4] = {
    rgb565(70, 240, 100), rgb565(255, 70, 55), rgb565(255, 225, 50), rgb565(80, 140, 255)};
  // LovyanGFX angles: 0 deg at 3 o'clock, clockwise. 4-deg gaps between pads.
  static const int16_t A0[4] = {182, 272, 92, 2};
  static const int16_t A1[4] = {268, 358, 178, 88};
  for (int i = 0; i < 4; i++) {
    _canvas.fillArc(CENTER_X, CENTER_Y, SIMON_RING_OUTER, SIMON_RING_INNER,
                    A0[i], A1[i], game.litColor() == i ? LIT[i] : DIM[i]);
  }

  // Center: big score up top, then Leon hopping, a hint and the exit button.
  char sc[8];
  snprintf(sc, sizeof(sc), "%d", game.score());
  _canvas.setTextSize(3);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setCursor(CENTER_X - _canvas.textWidth(sc) / 2, 44);
  _canvas.print(sc);

  // Leon hops in place (jump sprite while airborne). Bigger + centered.
  const unsigned long t = millis() % 900;
  int hop = 0, mode = 0;
  if (t < 520) { hop = (int)(sinf((t / 520.0f) * 3.14159f) * 18.0f); mode = 2; }
  drawGameFerret(CENTER_X, 104 - hop, mode, false, 1.6f);

  _canvas.setTextSize(2);
  _canvas.setTextColor(menu::TEXT_DIM);
  const char* hint = game.state() == SimonGame::SHOWING ? "Observe..."
                   : game.state() == SimonGame::WAITING ? "Sua vez!" : "";
  if (*hint) {
    _canvas.setCursor(CENTER_X - _canvas.textWidth(hint) / 2, 140);
    _canvas.print(hint);
  }

  // exit button (x) at the bottom of the inner circle
  _canvas.fillCircle(CENTER_X, SIMON_EXIT_CY, 15, rgb565(62, 50, 92));
  _canvas.drawCircle(CENTER_X, SIMON_EXIT_CY, 15, BTN_BORDER);
  for (int o = -1; o <= 0; o++) {  // 2px-thick "x"
    _canvas.drawLine(CENTER_X - 6 + o, SIMON_EXIT_CY - 6, CENTER_X + 6 + o, SIMON_EXIT_CY + 6, TFT_WHITE);
    _canvas.drawLine(CENTER_X + 6 + o, SIMON_EXIT_CY - 6, CENTER_X - 6 + o, SIMON_EXIT_CY + 6, TFT_WHITE);
  }

  if (game.gameOver()) {
    _canvas.fillRoundRect(34, 86, 172, 72, 10, menu::CLOCK_BG);
    _canvas.drawRoundRect(34, 86, 172, 72, 10, menu::CLOCK_EDGE);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    const char* go = "Errou!";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(go) / 2, 94);
    _canvas.print(go);
    _canvas.setTextSize(1);
    _canvas.setTextColor(menu::CLOCK_DATE);
    char l[24];
    snprintf(l, sizeof(l), "Pontos: %d", game.score());
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
    const char* h = "toque p/ sair";
    _canvas.setCursor(CENTER_X - _canvas.textWidth(h) / 2, 144);
    _canvas.print(h);
  }

  endScreen();
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
