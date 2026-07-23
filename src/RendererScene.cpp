#include "Renderer.h"
#include "RendererShared.h"

// Renderer partial: the magic-forest scene - sky/celestials, forest, cabin,
// creatures (fireflies/butterflies/shooting star), the weather FX painted
// over the scene (clouds/rain/snow/hail/lightning/fog + palette tint) and
// the pet sprite itself.

using namespace theme;
using namespace ui;

// One Christmas string-light bulb, IDENTICAL treatment on the cabin eave and
// the tree so they read as the same set: a lit 2px bulb with a soft glow, or a
// dim dot when off. Only the "off" background differs (roof vs foliage).
static void fairyBulb(LGFX_Sprite& cv, int x, int y, uint16_t c, bool on,
                      uint16_t skyBottom, uint16_t dimBg) {
  if (on) {
    cv.fillCircle(x, y, 1, c);                             // 2px bulb
    cv.drawPixel(x, y - 2, lerp565(c, skyBottom, 0.45f));  // glow
  } else {
    cv.drawPixel(x, y, lerp565(c, dimBg, 0.6f));           // dim (never gone)
  }
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

void Renderer::drawFerret(FerretActor& ferret) {
  const uint16_t* fr = ferret.frame();
  if (!fr) return;
  _canvas.pushImage(ferret.x(), ferret.y(), ferret.w(), ferret.h(),
                    fr, ferret.transparentKey());
}

// ---- Festive scene decorations ---------------------------------------------
// Procedural, in the scene's own fine-grain style (cabin/mushroom family).

// Christmas: blinking lights on the cabin roof, a star on the big pine,
// gift boxes by the door and - the magic - Santa's sleigh silhouette flying
// across the sky every ~40s (red nose blinking).
// Shared festive fairy-light palette (8 colors) - used on both the cabin
// eave and the tree so they read as one set of string lights.
static const uint16_t FAIRY[8] = {
    rgb565(240, 60, 60),   // red
    rgb565(80, 220, 100),  // green
    rgb565(90, 150, 250),  // blue
    rgb565(250, 210, 80),  // yellow
    rgb565(240, 120, 200), // pink
    rgb565(120, 230, 230), // cyan
    rgb565(250, 150, 60),  // orange
    rgb565(190, 130, 240), // purple
};
void Renderer::drawXmasDecor(bool night) {
  const unsigned long ms = millis();
  // Fairy-light string hung along the eave: same look as the tree lights -
  // small 1px bulbs with a soft glow when lit, dim (not gone) when off, on a
  // faint wire. (The old version was big blobs with no glow - it clashed
  // with the tree.)
  int px = 0, py = 0;
  for (int i = 0; i <= 8; i++) {  // both roof slopes, 4+1+4 bulbs
    const float t = i / 8.0f;
    int x, y;
    if (t <= 0.5f) { x = 25 + (int)(27 * t * 2); y = 86 - (int)(15 * t * 2); }
    else           { x = 52 + (int)(27 * (t - 0.5f) * 2); y = 71 + (int)(15 * (t - 0.5f) * 2); }
    y += 2;
    if (i) _canvas.drawLine(px, py, x, y, lerp565(_p.cabinRoof, _p.skyBottom, 0.3f));
    px = x; py = y;
    const bool on = ((ms / 400) + i) % 3 != 0;
    const uint16_t c = FAIRY[(i * 3) % 8];  // spread colors along the string
    fairyBulb(_canvas, x, y, c, on, _p.skyBottom, _p.cabinRoof);
  }
  // --- the big pine (210) becomes a decorated Christmas tree: baubles
  // hung across the foliage + blinking fairy lights + a star on top. The
  // tree spans roughly y 60..108, widening toward the base. ---
  const int tx = 210;
  static const uint16_t BAUBLE[4] = {rgb565(220, 50, 60), rgb565(240, 200, 70),
                                     rgb565(80, 150, 230), rgb565(230, 230, 240)};
  // baubles: placed within the triangular silhouette (half-width grows with y)
  struct O { int8_t dx, dy, c; };
  static const O baubles[7] = {
      {-3, 72, 0}, {4, 78, 1}, {-7, 86, 2}, {3, 88, 3},
      {-1, 94, 0}, {9, 96, 1}, {-12, 98, 3}};
  for (const auto& o : baubles) {
    _canvas.fillCircle(tx + o.dx, o.dy, 2, BAUBLE[o.c]);
    _canvas.drawPixel(tx + o.dx - 1, o.dy - 1, rgb565(255, 255, 255));  // highlight
    _canvas.drawPixel(tx + o.dx, o.dy - 2, _p.treeTrunk);               // cap
  }
  // fairy lights: little dots blinking in rolling phases across the tree
  static const O lights[8] = {
      {1, 68, 0}, {-5, 80, 1}, {7, 82, 1}, {-3, 90, 0},
      {-10, 92, 1}, {5, 94, 0}, {13, 96, 1}, {-6, 98, 0}};
  for (int i = 0; i < 8; i++) {
    const int lx = tx + lights[i].dx, ly = lights[i].dy;
    // Same bulb + same blink cadence (ms/400) as the cabin - only the color
    // phase and the dim background (foliage) differ.
    const bool on = ((ms / 400) + i) % 3 != 0;
    const uint16_t c = FAIRY[(i * 5 + 2) % 8];
    fairyBulb(_canvas, lx, ly, c, on, _p.skyBottom, _p.treeNear);
  }
  // bright twinkling star on top (~59)
  const uint16_t gold = rgb565(252, 224, 96), glow = rgb565(255, 245, 170);
  const int sx = tx, sy = 58;
  const int r = ((ms / 500) % 2) ? 5 : 4;
  _canvas.fillCircle(sx, sy, 2, glow);
  _canvas.drawFastHLine(sx - r, sy, 2 * r + 1, gold);
  _canvas.drawFastVLine(sx, sy - r, 2 * r + 1, gold);
  for (int d = 1; d < r - 1; d++) {
    _canvas.drawPixel(sx - d, sy - d, gold); _canvas.drawPixel(sx + d, sy - d, gold);
    _canvas.drawPixel(sx - d, sy + d, gold); _canvas.drawPixel(sx + d, sy + d, gold);
  }

  // gift boxes by the cabin door (ribbon cross + bow)
  struct G { int16_t x, y, w, h; uint16_t box, rib; };
  static const G gifts[3] = {
      {56, 116, 13, 11, rgb565(200, 60, 70), rgb565(250, 210, 80)},
      {70, 119, 10, 8, rgb565(80, 150, 220), rgb565(240, 240, 240)},
      {60, 108, 9, 8, rgb565(90, 190, 110), rgb565(200, 60, 70)},
  };
  for (const auto& g : gifts) {
    _canvas.fillRect(g.x, g.y, g.w, g.h, g.box);
    _canvas.drawFastVLine(g.x + g.w / 2, g.y, g.h, g.rib);
    _canvas.drawFastHLine(g.x, g.y + g.h / 2, g.w, g.rib);
    _canvas.drawPixel(g.x + g.w / 2 - 1, g.y - 1, g.rib);
    _canvas.drawPixel(g.x + g.w / 2 + 1, g.y - 1, g.rib);
  }

  // --- snowman on the grass (carrot nose, coal smile, red scarf). Raised so
  // his base sits ABOVE Leon's feet (FLOOR_Y): he reads as further back and
  // Leon cleanly passes in FRONT of him. ---
  {
    const int sx2 = 96, sy2 = 113;
    const uint16_t snow = rgb565(240, 244, 250), snowSh = rgb565(198, 206, 224);
    _canvas.fillEllipse(sx2, sy2 + 7, 8, 2, rgb565(20, 24, 20));   // shadow
    _canvas.fillCircle(sx2, sy2 + 2, 6, snow);                     // body
    _canvas.fillCircle(sx2 - 2, sy2 + 3, 5, snowSh);               // body shade
    _canvas.fillCircle(sx2, sy2 + 2, 5, snow);
    _canvas.fillCircle(sx2, sy2 - 6, 4, snow);                     // head
    _canvas.drawPixel(sx2 - 1, sy2 - 7, rgb565(30, 30, 34));       // eyes
    _canvas.drawPixel(sx2 + 1, sy2 - 7, rgb565(30, 30, 34));
    _canvas.drawFastHLine(sx2, sy2 - 5, 3, rgb565(245, 130, 40));  // carrot
    _canvas.drawPixel(sx2 - 1, sy2 - 4, rgb565(30, 30, 34));       // smile
    _canvas.drawPixel(sx2, sy2 - 3, rgb565(30, 30, 34));
    _canvas.drawPixel(sx2 + 1, sy2 - 4, rgb565(30, 30, 34));
    _canvas.drawFastHLine(sx2 - 3, sy2 - 2, 7, rgb565(200, 50, 60));  // scarf
    _canvas.drawFastVLine(sx2 + 2, sy2 - 1, 3, rgb565(200, 50, 60));
    _canvas.drawPixel(sx2, sy2 + 1, rgb565(30, 30, 34));           // buttons
    _canvas.drawPixel(sx2, sy2 + 3, rgb565(30, 30, 34));
    _canvas.drawLine(sx2 - 6, sy2, sx2 - 10, sy2 - 4, _p.treeTrunk);  // arms
    _canvas.drawLine(sx2 + 6, sy2, sx2 + 10, sy2 - 4, _p.treeTrunk);
  }
  // --- wreath on the cabin door + icicles under the eave ---
  {
    const uint16_t leaf = rgb565(40, 120, 60), leafHi = rgb565(70, 170, 90);
    _canvas.drawCircle(40, 111, 3, leaf);                    // wreath ring
    _canvas.drawCircle(40, 111, 2, leafHi);
    _canvas.drawPixel(40, 108, rgb565(200, 50, 60));         // red bow
    _canvas.drawPixel(39, 107, rgb565(200, 50, 60));
    _canvas.drawPixel(41, 107, rgb565(200, 50, 60));
    const uint16_t ice = rgb565(190, 225, 250);
    for (int i = 0; i < 4; i++) {                            // icicles
      const int ix = 30 + i * 11, ih = 2 + (i * 7) % 3;
      _canvas.drawFastVLine(ix, 88, ih, ice);
    }
  }
  // --- candy canes planted in the grass ---
  for (int c2 = 0; c2 < 2; c2++) {
    const int cx2 = 146 + c2 * 38, cy2 = 118 + (c2 ? 3 : 0);
    const uint16_t red = rgb565(220, 50, 60), wht = rgb565(245, 245, 248);
    for (int i = 0; i < 8; i++)  // striped stick
      _canvas.drawPixel(cx2, cy2 + i, (i / 2) % 2 ? red : wht);
    _canvas.drawPixel(cx2 - 1, cy2 - 1, wht);  // hook
    _canvas.drawPixel(cx2 - 2, cy2, red);
  }

  // Santa's sleigh crossing the sky: an 8s flight every ~75s (rare enough
  // that each pass feels special - and each one now goes out with a
  // "ho ho ho": drawXmas flags the flyby start, main plays the sound). The
  // route varies per pass (alternating direction, height and bob) and the
  // whole rig FACES the way it travels - reindeer up front, Rudolph's nose
  // leading, sleigh trailing behind. Deterministic on the pass index.
  const unsigned long PERIOD = 75000;
  const int pass = (int)(ms / PERIOD);
  const unsigned long cyc = ms % PERIOD;
  if (cyc < 8000) {
    if (pass != _lastFlybyPass) {  // new flight: cue the ho-ho-ho
      _lastFlybyPass = pass;
      _flybyPending = 1;
    }
    const float t = cyc / 8000.0f;
    const bool rightward = (pass % 2) == 1;         // alternate R->L and L->R
    const int s = rightward ? 1 : -1;              // travel / facing sign
    const int fx = rightward ? (-40 + (int)(300 * t)) : (260 - (int)(300 * t));
    const int fy = 26 + (pass % 3) * 8 +
                   (int)((4 + (pass % 2) * 3) * sinf(t * 6.28f));  // varied height + bob
    const uint16_t sil = night ? rgb565(20, 16, 28) : rgb565(60, 54, 70);
    // three reindeer ahead of fx (lead furthest along the travel direction)
    for (int i = 0; i < 3; i++) {
      const int rx = fx + s * (i * 9), ry = fy + (i % 2);
      _canvas.fillRect(rx - 2, ry, 5, 2, sil);        // body
      _canvas.drawPixel(rx + s * 3, ry - 1, sil);     // head (points forward)
      _canvas.drawPixel(rx + s * 4, ry - 2, sil);     // antler
      _canvas.drawFastVLine(rx - 1, ry + 2, 2, sil);  // legs
      _canvas.drawFastVLine(rx + 1, ry + 2, 2, sil);
    }
    if ((ms / 250) % 2)  // Rudolph's nose, leading the team
      _canvas.drawPixel(fx + s * (2 * 9 + 4), fy, rgb565(255, 60, 50));
    // harness + sleigh trailing BEHIND fx (opposite the travel direction)
    _canvas.drawLine(fx - s * 4, fy + 1, fx - s * 26, fy + 3, sil);      // harness
    _canvas.fillRect(fx + (rightward ? -38 : 26), fy + 1, 7, 3, sil);   // sleigh box
    _canvas.drawFastHLine(fx + (rightward ? -39 : 25), fy + 4, 9, sil); // runner
  }
}

// Halloween: the glowing pumpkin, two tombstones, a half-buried skull and a
// witch cauldron bubbling GREEN smoke (brighter and pulsing after dark).
void Renderer::drawHalloweenDecor(bool night) {
  const unsigned long ms = millis();
  // --- graveyard cluster on the LEFT (raised + spread out so the pieces
  // breathe; clear of the pumpkin/cauldron) ---
  const uint16_t stone = rgb565(140, 142, 154), stoneDk = rgb565(92, 94, 106);
  // rounded headstone, casting a small shadow (raised 1px so it clears the
  // forest mushroom that sits on the grass just below)
  _canvas.fillEllipse(35, 120, 12, 3, rgb565(20, 24, 20));          // shadow
  _canvas.fillRoundRect(28, 97, 15, 22, 6, stone);
  _canvas.fillRect(28, 107, 15, 12, stone);
  // "RIP" hand-pixeled 3x5 so it fits inside the 15px stone (textSize 1 = 18px
  // overflowed the edge). Rows are 3-bit masks, MSB = leftmost pixel.
  static const uint8_t RIP[3][5] = {
      {0b110, 0b101, 0b110, 0b101, 0b101},  // R
      {0b111, 0b010, 0b010, 0b010, 0b111},  // I
      {0b110, 0b101, 0b110, 0b100, 0b100},  // P
  };
  for (int gl = 0; gl < 3; gl++)
    for (int gy = 0; gy < 5; gy++)
      for (int gx = 0; gx < 3; gx++)
        if (RIP[gl][gy] & (1 << (2 - gx)))
          _canvas.drawPixel(30 + gl * 4 + gx, 103 + gy, stoneDk);
  // tilted cross grave, moved right + up (also 1px up, off the mushroom)
  _canvas.fillEllipse(56, 121, 9, 3, rgb565(20, 24, 20));
  _canvas.fillRect(54, 103, 4, 16, stoneDk);
  _canvas.fillRect(50, 107, 12, 3, stoneDk);
  // a clear skull sitting on the grass (readable: dark sockets + jaw), spread
  // further right so it doesn't crowd the cross
  const uint16_t bone = rgb565(236, 232, 216), boneDk = rgb565(40, 34, 40);
  _canvas.fillRoundRect(84, 114, 8, 7, 3, bone);
  _canvas.fillRect(85, 121, 6, 2, bone);                            // jaw
  _canvas.fillRect(86, 116, 2, 2, boneDk);                          // eyes
  _canvas.fillRect(89, 116, 2, 2, boneDk);
  _canvas.drawFastVLine(87, 119, 2, boneDk);                        // nose
  _canvas.drawFastVLine(88, 122, 1, boneDk);                        // tooth gap

  // --- witch cauldron on the RIGHT, well clear of the cabin/Leon ---
  const int cx = 206, cy = 122;
  const float pulse = 0.5f + 0.5f * sinf(ms / 500.0f);
  const uint16_t brew = night ? lerp565(rgb565(70, 220, 90), rgb565(170, 255, 150), pulse)
                              : rgb565(80, 175, 90);
  // big ground glow so it reads as MAGIC
  _canvas.fillEllipse(cx, cy + 5, 20, 6,
                      lerp565(_p.ground, rgb565(70, 210, 80), (night ? 0.45f : 0.2f) * pulse + 0.18f));
  _canvas.fillEllipse(cx, cy, 12, 8, rgb565(34, 34, 42));           // pot (bigger)
  _canvas.fillRect(cx - 12, cy - 6, 25, 3, rgb565(50, 50, 60));     // rim
  _canvas.drawFastHLine(cx - 10, cy - 7, 21, brew);                 // brew
  _canvas.drawFastHLine(cx - 9, cy - 8, 19, lerp565(brew, rgb565(220, 255, 200), 0.5f));
  // three clawed cast-iron feet splaying outward, each ending in a ball
  const uint16_t iron = rgb565(28, 28, 34);
  for (int f = -1; f <= 1; f++) {
    const int fx2 = cx + f * 8;
    _canvas.drawFastVLine(fx2, cy + 6, 3, iron);   // leg
    _canvas.drawPixel(fx2 + f, cy + 9, iron);      // outward curve
    _canvas.fillCircle(fx2 + f, cy + 10, 1, iron); // ball foot
  }
  for (int i = 0; i < 3; i++) {  // bubbles
    const int b = (int)((ms / 150 + i * 5) % 10);
    if (b < 5) _canvas.fillCircle(cx - 6 + i * 6 + b, cy - 8, 1, brew);
  }
  // occasional POP: a bubble swells on the surface then bursts into a little
  // green flash of flying sparks (wandering site, all deterministic on ms).
  const int ep = (int)((ms / 80) % 22);                     // ~1.8s pop cycle
  const int ex = cx - 6 + (int)((ms / 1760) % 3) * 6;       // wandering pop site
  const uint16_t flash = lerp565(brew, rgb565(235, 255, 215), 0.75f);
  if (ep < 4) {
    _canvas.fillCircle(ex, cy - 8, ep < 2 ? 1 : 2, flash);  // swelling bubble
  } else if (ep < 9) {
    const int r = ep - 3;                                    // 1..5 expanding
    _canvas.drawPixel(ex - r, cy - 8, flash);
    _canvas.drawPixel(ex + r, cy - 8, flash);
    _canvas.drawPixel(ex, cy - 8 - r, flash);
    _canvas.drawPixel(ex - r / 2, cy - 9, lerp565(flash, _p.skyBottom, r / 6.0f));
    _canvas.drawPixel(ex + r / 2, cy - 9, lerp565(flash, _p.skyBottom, r / 6.0f));
  }
  // thick green smoke column rising and fading
  for (int i = 0; i < 4; i++) {
    const int c2 = (int)((ms / 90 + i * 7) % 32);
    if (c2 > 26) continue;
    const int sy2 = cy - 10 - c2;
    const int sx2 = cx + (int)(4.0f * sinf(ms / 650.0f + i * 1.7f));
    _canvas.fillCircle(sx2, sy2, c2 < 12 ? 2 : 3,
                       lerp565(brew, _p.skyBottom, c2 / 32.0f));
  }

  // --- the carved pumpkin, centered ---
  const int px = 120, py = 122;
  const uint16_t orange = rgb565(232, 120, 34), rib = rgb565(190, 88, 22);
  _canvas.fillEllipse(px, py, 10, 8, orange);
  _canvas.drawFastVLine(px - 4, py - 6, 12, rib);
  _canvas.drawFastVLine(px + 4, py - 6, 12, rib);
  _canvas.fillRect(px - 1, py - 10, 3, 3, rgb565(96, 128, 52));
  uint16_t face;
  if (night) {
    const float pu = 0.5f + 0.5f * sinf(ms / 500.0f);
    face = lerp565(rgb565(255, 150, 40), rgb565(255, 235, 120), pu);
  } else {
    face = rgb565(70, 34, 12);
  }
  _canvas.fillTriangle(px - 5, py - 2, px - 2, py - 2, px - 3, py - 5, face);
  _canvas.fillTriangle(px + 2, py - 2, px + 5, py - 2, px + 3, py - 5, face);
  _canvas.drawFastHLine(px - 4, py + 3, 9, face);
  _canvas.drawPixel(px - 2, py + 2, face);
  _canvas.drawPixel(px + 1, py + 4, face);
  _canvas.drawPixel(px + 3, py + 2, face);

  // --- the WITCH crossing the sky on her broom (Santa's-sleigh mechanic:
  // 8s flight every ~75s, varied route, faces her travel direction, and her
  // cackle plays as she appears). Green sparkle trail behind the broom. ---
  const unsigned long PERIOD = 75000;
  const int pass = (int)(ms / PERIOD);
  const unsigned long cyc = ms % PERIOD;
  if (cyc < 8000) {
    if (pass != _lastFlybyPass) {  // new flight: cue the cackle
      _lastFlybyPass = pass;
      _flybyPending = 2;
    }
    const float t = cyc / 8000.0f;
    const bool rightward = (pass % 2) == 1;
    const int s = rightward ? 1 : -1;
    const int wx = rightward ? (-30 + (int)(290 * t)) : (250 - (int)(290 * t));
    const int wy = 30 + (pass % 3) * 9 +
                   (int)((5 + (pass % 2) * 3) * sinf(t * 6.28f));
    const uint16_t sil = night ? rgb565(24, 18, 34) : rgb565(58, 48, 72);
    // broomstick along the travel direction; bristles at the back
    _canvas.drawFastHLine(wx - (rightward ? 8 : 4), wy + 2, 12, sil);
    for (int b = 0; b < 3; b++)  // bristle fan
      _canvas.drawLine(wx - s * 8, wy + 2, wx - s * 11, wy + b, sil);
    // witch sitting on the stick: torso, head, pointed hat leaning back
    _canvas.fillRect(wx - 1, wy - 2, 3, 4, sil);              // torso
    _canvas.drawPixel(wx + s, wy - 3, sil);                   // head
    _canvas.fillTriangle(wx + s, wy - 4, wx + s * 3, wy - 4,  // hat brim->tip
                         wx - s, wy - 7, sil);
    _canvas.drawLine(wx - s, wy - 1, wx - s * 4, wy + 1, sil);  // cape flutter
    _canvas.drawPixel(wx + s * 3, wy + 1, sil);               // hands on stick
    // sparkle trail: green motes fading behind her
    for (int i = 1; i <= 3; i++) {
      const int tx2 = wx - s * (12 + i * 5 + (int)((ms / 90 + i * 3) % 4));
      const int ty2 = wy + 1 + (int)(2.0f * sinf(ms / 150.0f + i * 2.0f));
      _canvas.drawPixel(tx2, ty2,
                        lerp565(rgb565(90, 230, 110), _p.skyBottom, i / 4.0f));
    }
  }
}

// Festa junina: the flag garland, a crackling BONFIRE and paper sky
// lanterns (baloezinhos) drifting up into the night.
void Renderer::drawJuninaDecor(bool night) {
  static const uint16_t FLAGS[4] = {rgb565(235, 70, 70), rgb565(250, 210, 80),
                                    rgb565(90, 200, 110), rgb565(90, 140, 250)};
  const unsigned long ms = millis();
  // --- two flag garlands: cabin->pine and pine->pine (a real arraial) ---
  const uint16_t rope = rgb565(120, 96, 70);
  struct GL { int16_t x0, y0, x1, y1; uint8_t sag; };
  static const GL garlands[2] = {{52, 64, 90, 68, 10}, {90, 68, 210, 62, 12}};
  for (const auto& gl : garlands) {
    int px = gl.x0, py = gl.y0;
    for (int i = 1; i <= 10; i++) {
      const float t = i / 10.0f;
      const int x = gl.x0 + (int)((gl.x1 - gl.x0) * t);
      const int y = gl.y0 + (int)((gl.y1 - gl.y0) * t) + (int)(gl.sag * 4 * t * (1 - t));
      _canvas.drawLine(px, py, x, y, rope);
      if (i < 10 && (i % 2) == 1)
        _canvas.fillTriangle(x - 3, y + 1, x + 3, y + 1, x, y + 7, FLAGS[(i / 2 + (gl.sag & 1)) % 4]);
      px = x; py = y;
    }
  }

  // --- barraquinha (striped festival stall) center-left, with a lit lamp ---
  {
    const int bx = 96, bg = 128;  // left edge, ground line
    const uint16_t post = rgb565(140, 105, 70);
    const uint16_t red = rgb565(215, 70, 70), wht = rgb565(246, 240, 230);
    _canvas.fillEllipse(bx + 14, bg + 1, 17, 3, rgb565(24, 30, 22));  // shadow
    _canvas.drawFastVLine(bx, bg - 22, 22, post);                     // posts
    _canvas.drawFastVLine(bx + 28, bg - 22, 22, post);
    _canvas.fillRect(bx - 1, bg - 10, 31, 4, rgb565(160, 120, 80));   // counter
    _canvas.drawFastHLine(bx - 1, bg - 10, 31, rgb565(190, 150, 100));
    for (int i = 0; i < 7; i++)                                        // awning
      _canvas.fillRect(bx - 2 + i * 5, bg - 26, 5, 5, (i % 2) ? wht : red);
    for (int i = 0; i < 7; i++)                                        // scallop
      _canvas.drawPixel(bx + i * 5, bg - 21, (i % 2) ? wht : red);
    // warm lamp under the awning (glows at night)
    if (night) _canvas.fillCircle(bx + 14, bg - 17, 3, lerp565(_p.skyBottom, rgb565(255, 200, 90), 0.55f));
    _canvas.drawPixel(bx + 14, bg - 18, rgb565(255, 215, 120));
    // goodies on the counter (paçoca/quentão cups)
    _canvas.drawPixel(bx + 6, bg - 11, rgb565(230, 180, 90));
    _canvas.drawPixel(bx + 14, bg - 11, rgb565(200, 90, 60));
    _canvas.drawPixel(bx + 22, bg - 11, rgb565(230, 180, 90));
  }

  // --- corn stalks (pe de milho) by the stall ---
  for (int c2 = 0; c2 < 2; c2++) {
    const int mx = 140 + c2 * 16, mg = 127 + (c2 ? 1 : 0);
    const uint16_t stalk = rgb565(90, 150, 60), leafC = rgb565(120, 180, 80);
    _canvas.drawFastVLine(mx, mg - 12, 12, stalk);
    _canvas.drawLine(mx, mg - 8, mx - 3, mg - 11, leafC);   // leaves
    _canvas.drawLine(mx, mg - 5, mx + 3, mg - 8, leafC);
    _canvas.drawFastVLine(mx + 1, mg - 9, 3, rgb565(240, 210, 90));  // cob
  }

  // --- BONFIRE (fogueira): the centerpiece - a real bright fire on the
  // right, clear of Leon's usual paths. Big flames, layered core, sparks. ---
  const int fx = 200, fy = 128;
  const uint16_t log_ = _p.treeTrunk, logHi = lerp565(_p.treeTrunk, rgb565(150, 100, 60), 0.5f);
  // warm ground glow (day too, stronger at night), breathing with the fire
  const float g = (night ? 0.45f : 0.22f) + 0.18f * sinf(ms / 260.0f);
  _canvas.fillEllipse(fx, fy + 2, 22, 6, lerp565(_p.ground, rgb565(250, 150, 50), g));
  // stacked logs (thicker, two-tone)
  _canvas.fillRect(fx - 12, fy, 24, 4, log_);
  _canvas.drawLine(fx - 11, fy + 3, fx + 9, fy - 3, logHi);
  _canvas.drawLine(fx - 9, fy - 3, fx + 11, fy + 3, logHi);
  // big layered flame: outer, mid, hot yellow core - each tongue flickers
  static const uint16_t FIRE[4] = {rgb565(210, 60, 20), rgb565(245, 120, 30),
                                    rgb565(252, 180, 50), rgb565(255, 235, 150)};
  for (int layer = 0; layer < 4; layer++) {
    const int spread = 11 - layer * 2;
    const int base = 14 - layer * 2;              // outer tallest
    for (int i = -1; i <= 1; i++) {
      const int flick = (int)((ms / 80 + (i + 2) * 29 + layer * 13) % 6);
      const int h = base + flick;
      const int bx2 = fx + i * spread / 2;
      _canvas.fillTriangle(bx2 - spread / 3 - 1, fy, bx2 + spread / 3 + 1, fy,
                           bx2 + (int)(2.0f * sinf(ms / 220.0f + i + layer)), fy - h,
                           FIRE[layer]);
    }
  }
  // rising sparks
  for (int i = 0; i < 4; i++) {
    const int c2 = (int)((ms / 60 + i * 13) % 26);
    if (c2 > 20) continue;
    _canvas.drawPixel(fx - 6 + i * 4 + (int)(3.0f * sinf(ms / 180.0f + i)),
                      fy - 16 - c2, c2 < 10 ? FIRE[3] : FIRE[1]);
  }

  // --- paper sky lanterns (baloezinhos): bigger, warm, glowing, drifting
  // up across the whole sky ---
  for (int i = 0; i < 4; i++) {
    const int cyc = (int)((ms / 40 + i * 230) % 920);
    const int by2 = 122 - cyc / 6;
    if (by2 < -12) continue;
    const int bx2 = 30 + i * 58 + (int)(7.0f * sinf(ms / 1500.0f + i * 2.0f));
    const uint16_t c = FLAGS[(i * 3 + 1) % 4];
    const float lit = (night && (ms / 140 + i) % 4) ? 1.0f : 0.5f;
    if (night)  // soft halo
      _canvas.fillEllipse(bx2, by2, 5, 6, lerp565(_p.skyBottom, c, 0.35f * lit));
    _canvas.fillEllipse(bx2, by2 - 1, 4, 5, c);          // paper body (bigger)
    _canvas.drawFastHLine(bx2 - 2, by2 + 4, 5, lerp565(c, rgb565(40, 30, 20), 0.6f));
    _canvas.drawPixel(bx2, by2 + 3, rgb565(255, 235, 140));  // flame
    _canvas.drawPixel(bx2 - 3, by2 + 5, rgb565(120, 100, 70));  // strings
    _canvas.drawPixel(bx2 + 3, by2 + 5, rgb565(120, 100, 70));
  }
}

// New Year's Eve/Day: FIREWORKS. Three launch sites cycle independently -
// rocket streaks up with a spark trail, bursts into an expanding shell of
// colored sparks that sag and fade, then the site goes dark and relaunches.
// All deterministic on ms (no rand); positions/colors rotate per launch.
void Renderer::drawNyeDecor(bool night) {
  const unsigned long ms = millis();
  static const uint16_t SHELL[6] = {
      rgb565(255, 210, 80),   // gold
      rgb565(240, 80, 90),    // red
      rgb565(110, 220, 240),  // cyan
      rgb565(200, 120, 250),  // purple
      rgb565(120, 240, 130),  // green
      rgb565(250, 150, 220),  // pink
  };
  for (int site = 0; site < 3; site++) {
    // each site: ~3.2s cycle, staggered
    const unsigned long cyc = (ms + site * 1100) % 3200;
    const int launch = (int)((ms + site * 1100) / 3200);
    // launch position varies per cycle (deterministic hash of launch idx)
    const int bx = 40 + ((launch * 37 + site * 71) % 160);   // burst x 40..199
    const int by = 38 + ((launch * 53 + site * 29) % 34);    // burst y 38..71
    const uint16_t col = SHELL[(launch + site * 2) % 6];
    if (cyc < 900) {
      // rocket rising from the treeline: bright 2px head + glowing tail
      const float t = cyc / 900.0f;
      const int ry = 110 - (int)((110 - by) * t);
      const int rx = bx + (int)(3.0f * sinf(t * 9.0f + site));  // wiggle
      _canvas.fillRect(rx, ry, 2, 2, rgb565(255, 250, 230));    // head
      _canvas.drawFastVLine(rx, ry + 2, 3, rgb565(255, 200, 110));  // tail
      _canvas.drawPixel(rx + 1, ry + 5, lerp565(rgb565(255, 170, 80), _p.skyBottom, 0.4f));
      _canvas.drawPixel(rx - 1, ry + 6, lerp565(rgb565(255, 170, 80), _p.skyBottom, 0.6f));
    } else if (cyc < 2500) {
      // BURST: 12 rays exploding from the core - each ray is a line segment
      // racing outward (reads as a real firework, not lonely dots), with a
      // bright white tip; old sparks detach and sag with gravity.
      const float t = (cyc - 900) / 1600.0f;      // 0..1
      const int r = 5 + (int)(22 * t);
      const int sag = (int)(8 * t * t);           // gravity pull
      const uint16_t rayC = lerp565(col, _p.skyTop, t * 0.45f);  // stays bold
      const uint16_t tipC = lerp565(rgb565(255, 255, 235), col, t);
      for (int k = 0; k < 16; k++) {
        const float a = k * 0.3927f + (site * 0.26f);  // 22.5 deg + site twist
        const float ca = cosf(a), sa = sinf(a) * 0.85f;
        const int x1 = bx + (int)(r * ca), y1 = by + (int)(r * sa) + sag;
        if (t < 0.6f) {  // young burst: full rays from the core outward
          const int r0 = (int)(r * 0.4f);
          _canvas.drawLine(bx + (int)(r0 * ca), by + (int)(r0 * sa) + sag / 2,
                           x1, y1, rayC);
        } else {         // old burst: detached fat sparks falling
          _canvas.fillRect(x1, y1, 2, 2, rayC);
          _canvas.drawPixel(x1, y1 + 2, lerp565(rayC, _p.skyTop, 0.5f));
        }
        _canvas.fillRect(x1, y1, 2, 2, tipC);  // bright tip
      }
      // hot flash core right after the pop, with a real glow ring
      if (t < 0.22f) {
        _canvas.fillCircle(bx, by, 3, rgb565(255, 252, 235));
        _canvas.drawCircle(bx, by, 5, lerp565(rgb565(255, 255, 255), col, 0.4f));
        if (night)  // whole-sky glow at night
          _canvas.fillCircle(bx, by, 8, lerp565(_p.skyTop, col, 0.30f));
      }
    }
  }
  // golden confetti drifting over the scene (lighter than the bday one)
  for (int i = 0; i < 8; i++) {
    const int cx2 = (i * 73 + (int)(5.0f * sinf(ms / 900.0f + i))) % SCREEN_W;
    const int cy2 = (int)((ms / 40 + i * 97) % (GROUND_Y + 10));
    _canvas.drawPixel(cx2, cy2, SHELL[i % 6]);
  }
}

// Birthday: THE CAKE (two tiers, dripping frosting, flickering candles),
// balloons drifting freely around the sky and confetti everywhere.
void Renderer::drawParty(bool night) {
  static const uint16_t BALLOON[3] = {rgb565(240, 84, 120), rgb565(250, 210, 80),
                                      rgb565(90, 170, 240)};
  const unsigned long ms = millis();

  // --- balloons wandering the sky (slow Lissajous, firefly-style) ---
  struct B { int16_t cx, cy, ax, ay; uint16_t t1, t2; float ph; };
  static const B bs[3] = {
      {52, 52, 26, 14, 5200, 3900, 0.0f},
      {120, 40, 34, 12, 6100, 4700, 2.1f},
      {192, 56, 24, 16, 5600, 4300, 4.2f},
  };
  for (int i = 0; i < 3; i++) {
    const B& b = bs[i];
    const int bx = b.cx + (int)(b.ax * sinf(ms / (float)b.t1 + b.ph));
    const int by = b.cy + (int)(b.ay * sinf(ms / (float)b.t2 + b.ph * 1.7f));
    _canvas.fillEllipse(bx, by, 6, 8, BALLOON[i]);
    _canvas.drawPixel(bx - 2, by - 3, rgb565(255, 255, 255));
    _canvas.fillTriangle(bx - 1, by + 8, bx + 1, by + 8, bx, by + 10, BALLOON[i]);
    for (int s2 = 0; s2 < 10; s2 += 2)
      _canvas.drawPixel(bx + (int)(1.5f * sinf(ms / 700.0f + s2)), by + 11 + s2,
                        rgb565(200, 200, 210));
  }

  // --- the cake (mandatory!) ---
  const int kx = 120, ky = 118;
  const uint16_t plate = rgb565(210, 210, 220);
  const uint16_t tier1 = rgb565(238, 150, 180), tier2 = rgb565(180, 120, 210);
  const uint16_t frost = rgb565(246, 240, 230);
  _canvas.fillRoundRect(kx - 14, ky + 6, 29, 3, 1, plate);       // plate
  _canvas.fillRect(kx - 12, ky - 2, 25, 8, tier1);               // bottom tier
  _canvas.fillRect(kx - 12, ky - 2, 25, 3, frost);               // frosting
  _canvas.drawPixel(kx - 8, ky + 2, frost);                      // drips
  _canvas.drawPixel(kx - 1, ky + 1, frost);
  _canvas.drawPixel(kx + 7, ky + 2, frost);
  _canvas.fillRect(kx - 7, ky - 8, 15, 6, tier2);                // top tier
  _canvas.fillRect(kx - 7, ky - 8, 15, 2, frost);
  for (int i = 0; i < 3; i++)  // frosting dots on the bottom tier
    _canvas.fillCircle(kx - 8 + i * 8, ky - 1, 1, rgb565(255, 120, 160));
  // three candles with LIVE flames: each sways side to side on its own
  // clock and twinkles through warm tones (never fully out - a real flame
  // flickers in brightness, it doesn't blink off).
  for (int i = 0; i < 3; i++) {
    const int cx2 = kx - 4 + i * 4;
    _canvas.drawFastVLine(cx2, ky - 12, 4, BALLOON[i]);         // wax stick
    _canvas.drawPixel(cx2, ky - 13, rgb565(60, 60, 70));        // wick
    // sway: the tip leans -1/0/+1 px, each candle out of phase
    const float sway = sinf(ms / 260.0f + i * 2.1f);
    const int tipx = cx2 + (int)roundf(sway);
    // twinkle: brightness rides a fast flicker, tinting the flame body
    const float tw = 0.5f + 0.5f * sinf(ms / 90.0f + i * 3.7f);
    const uint16_t hot = lerp565(rgb565(250, 150, 40), rgb565(255, 240, 170), tw);
    if (night)  // soft glow halo around the flame
      _canvas.fillCircle(tipx, ky - 15, 2, lerp565(_p.skyBottom, rgb565(255, 190, 90), 0.5f));
    _canvas.drawPixel(cx2, ky - 14, rgb565(255, 120, 40));      // base (steady)
    _canvas.drawPixel(tipx, ky - 15, hot);                      // mid
    _canvas.drawPixel(tipx, ky - 16, lerp565(hot, rgb565(255, 255, 210), 0.6f));  // tip
  }

  // --- confetti raining over the whole scene ---
  for (int i = 0; i < 14; i++) {
    const int cx2 = (i * 61 + (int)(6.0f * sinf(ms / 800.0f + i))) % SCREEN_W;
    const int cy2 = (int)((ms / 28 + i * 83) % (GROUND_Y + 20));
    _canvas.drawPixel(cx2, cy2, BALLOON[i % 3]);
  }
}
