#include "Renderer.h"
#include "RendererShared.h"
#include "accessory_sprites.h"  // bed/blanket/hats (base-sprite style)

// Renderer partial: the magic-forest scene - sky/celestials, forest, cabin,
// creatures (fireflies/butterflies/shooting star), the weather FX painted
// over the scene (clouds/rain/snow/hail/lightning/fog + palette tint) and
// the pet sprite itself.

using namespace theme;
using namespace ui;

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

// ---- Accessories & festive art ---------------------------------------------
// All procedural (house style): the bed/blanket/hats anchor to the ferret's
// frame (x(), y(), faceLeft(), animName()) so they ride every animation.

// Striped blanket sprite over the sleeping body (tail side), with a gentle
// 1px "breathing" ride so it feels alive.
void Renderer::drawBlanket(FerretActor& f) {
  const int breathe = ((millis() / 900) % 2) ? 1 : 0;
  _canvas.pushImage(f.x() + 8, 111 + breathe, acc_blanket_w, acc_blanket_h,
                    acc_blanket, (uint16_t)0xF81F);
}

// Hats: pixel-art sprites in the base style, anchored to the HEAD-TOP of the
// exact current frame (anchors scanned from the sprite sheet - the hat rides
// every bob of the walk and the whole jump arc).
void Renderer::drawHat(FerretActor& f) {
  // No hat mid-burrow (the head is underground). And during the jump's
  // somersault (frames 1-4, head tumbling) the hat "pops off" - cartoon
  // physics beats a hat glued to a flipping skull.
  const char* an = f.animName();
  if (!strcmp(an, "disappear") || !strcmp(an, "emerge")) return;
  if (!strcmp(an, "jump")) {
    const uint8_t fi = f.frameIndex();
    if (fi >= 1 && fi <= 4) return;
  }

  // Which hat right now? Nightcap wins while tucked in.
  Hat h = HAT_NONE;
  if (f.inBed()) h = HAT_SLEEP;
  else if (_bdayMode) h = HAT_PARTY;
  else if (_fest == FEST_NATAL) h = HAT_SANTA;
  else if (_fest == FEST_JUNINA) h = HAT_PALHA;
  else if (_fest == FEST_HALLOWEEN) h = HAT_BRUXA;
  if (h == HAT_NONE) return;

  int ax, ay;  // head-top in 1x sprite coords
  if (!accessoryAnchor(an, f.faceLeft(), f.frameIndex(), &ax, &ay)) return;

  // Tilted hats (santa/witch/nightcap tips, authored leaning RIGHT) must
  // flop BACKWARD, away from the face: right-facing ferret wears the
  // mirrored (left-leaning) variant and vice versa. Symmetric hats
  // (party/straw) just pick either.
  const bool L = f.faceLeft();
  const uint16_t* spr;
  int w, hgt;
  switch (h) {
    case HAT_SLEEP: spr = hat_sleep_l; w = hat_sleep_w; hgt = hat_sleep_h; break;
    case HAT_PARTY: spr = hat_party; w = hat_party_w; hgt = hat_party_h; break;
    case HAT_SANTA: spr = L ? hat_santa : hat_santa_l; w = hat_santa_w; hgt = hat_santa_h; break;
    case HAT_PALHA: spr = hat_palha; w = hat_palha_w; hgt = hat_palha_h; break;
    default:        spr = L ? hat_bruxa : hat_bruxa_l; w = hat_bruxa_w; hgt = hat_bruxa_h; break;
  }
  // Position in 1x SPRITE space and scale once: the hat lands on the same
  // 2.5px pixel lattice as the ferret. Screen-space placement drifted by
  // sub-grid fractions per frame, which read as the hat floating.
  const int w1 = (int)roundf(w / 2.5f), h1 = (int)roundf(hgt / 2.5f);
  const int x1 = ax - w1 / 2;
  const int y1 = ay - h1 + 2;  // brim overlaps the skull by 2 source px
  _canvas.pushImage(f.x() + (int)roundf(x1 * 2.5f),
                    f.y() + (int)roundf(y1 * 2.5f), w, hgt, spr,
                    (uint16_t)0xF81F);
}

// Christmas: colored lights blinking along the cabin roof + a star on the
// big pine's tip. Cabin geometry mirrors drawCabin(30, GROUND_Y): eave line
// y=86 from x=25..79, apex (52, 71); big pine tip at (210, ~60).
void Renderer::drawXmasDecor(bool night) {
  static const uint16_t LIGHTS[4] = {rgb565(235, 60, 60), rgb565(80, 220, 100),
                                     rgb565(90, 140, 250), rgb565(250, 210, 80)};
  const unsigned long ms = millis();
  for (int i = 0; i <= 8; i++) {  // both slopes, 4+1+4 dots
    const float t = i / 8.0f;
    int x, y;
    if (t <= 0.5f) { x = 25 + (int)(27 * t * 2); y = 86 - (int)(15 * t * 2); }
    else           { x = 52 + (int)(27 * (t - 0.5f) * 2); y = 71 + (int)(15 * (t - 0.5f) * 2); }
    const bool on = ((ms / 450) + i) % 3 != 0;  // blink in rolling phases
    const uint16_t c = LIGHTS[i % 4];
    _canvas.fillCircle(x, y + 2, on ? 2 : 1, on ? c : lerp565(c, _p.cabinRoof, 0.6f));
  }
  // star on the big pine (210, 60), twinkling
  const uint16_t gold = rgb565(250, 220, 90);
  const int sx = 210, sy = 59;
  const int r = ((ms / 600) % 2) ? 3 : 2;
  _canvas.drawFastHLine(sx - r, sy, 2 * r + 1, gold);
  _canvas.drawFastVLine(sx, sy - r, 2 * r + 1, gold);
  _canvas.drawPixel(sx - 1, sy - 1, gold);
  _canvas.drawPixel(sx + 1, sy - 1, gold);
  _canvas.drawPixel(sx - 1, sy + 1, gold);
  _canvas.drawPixel(sx + 1, sy + 1, gold);
}

// Halloween: carved pumpkin on the grass; the face glows and pulses at night.
void Renderer::drawHalloweenDecor(bool night) {
  const int px = 104, py = 120;  // between cabin and bed, on the grass
  const uint16_t orange = rgb565(232, 120, 34), rib = rgb565(190, 88, 22);
  const uint16_t stem = rgb565(96, 128, 52);
  _canvas.fillEllipse(px, py, 10, 8, orange);
  _canvas.drawFastVLine(px - 4, py - 6, 12, rib);
  _canvas.drawFastVLine(px + 4, py - 6, 12, rib);
  _canvas.fillRect(px - 1, py - 10, 3, 3, stem);
  // face: glowing at night (pulse), dark carving by day
  uint16_t face;
  if (night) {
    const float pulse = 0.5f + 0.5f * sinf(millis() / 500.0f);
    face = lerp565(rgb565(255, 150, 40), rgb565(255, 235, 120), pulse);
  } else {
    face = rgb565(70, 34, 12);
  }
  _canvas.fillTriangle(px - 5, py - 2, px - 2, py - 2, px - 3, py - 5, face);  // eyes
  _canvas.fillTriangle(px + 2, py - 2, px + 5, py - 2, px + 3, py - 5, face);
  _canvas.drawFastHLine(px - 4, py + 3, 9, face);  // zigzag mouth
  _canvas.drawPixel(px - 2, py + 2, face);
  _canvas.drawPixel(px + 1, py + 4, face);
  _canvas.drawPixel(px + 3, py + 2, face);
}

// Festa junina: a sagging garland of little triangle flags strung between
// the two front pines (tips ~(90,67) and (210,60)).
void Renderer::drawJuninaDecor() {
  static const uint16_t FLAGS[4] = {rgb565(235, 70, 70), rgb565(250, 210, 80),
                                    rgb565(90, 200, 110), rgb565(90, 140, 250)};
  const int x0 = 90, y0 = 68, x1 = 210, y1 = 62;
  const uint16_t rope = rgb565(120, 96, 70);
  int px = x0, py = y0;
  for (int i = 1; i <= 10; i++) {
    const float t = i / 10.0f;
    const int x = x0 + (int)((x1 - x0) * t);
    // parabolic sag, deepest (+12px) at the middle
    const int y = y0 + (int)((y1 - y0) * t) + (int)(12 * 4 * t * (1 - t));
    _canvas.drawLine(px, py, x, y, rope);
    if (i < 10 && (i % 2) == 1) {  // a flag on every other segment
      _canvas.fillTriangle(x - 3, y + 1, x + 3, y + 1, x, y + 7, FLAGS[(i / 2) % 4]);
    }
    px = x; py = y;
  }
}

// Birthday: floating balloons (gentle bob) + falling confetti, in front of
// everything except the HUD.
void Renderer::drawParty() {
  static const uint16_t BALLOON[3] = {rgb565(240, 84, 120), rgb565(250, 210, 80),
                                      rgb565(90, 170, 240)};
  const unsigned long ms = millis();
  for (int i = 0; i < 3; i++) {
    const int bx = 44 + i * 74;
    const int by = 52 + (int)(4.0f * sinf(ms / 900.0f + i * 2.1f));
    _canvas.fillEllipse(bx, by, 6, 8, BALLOON[i]);
    _canvas.drawPixel(bx - 2, by - 3, rgb565(255, 255, 255));  // shine
    _canvas.fillTriangle(bx - 1, by + 8, bx + 1, by + 8, bx, by + 10, BALLOON[i]);
    // wavy string
    for (int s = 0; s < 12; s += 2)
      _canvas.drawPixel(bx + (int)(1.5f * sinf(ms / 700.0f + s)), by + 11 + s,
                        rgb565(200, 200, 210));
  }
  // confetti: sparse colored specks drifting down (snow-style hash motion)
  for (int i = 0; i < 14; i++) {
    const int cx = (i * 61 + (int)(6.0f * sinf(ms / 800.0f + i))) % SCREEN_W;
    const int cy = (int)((ms / 28 + i * 83) % (GROUND_Y + 20));
    _canvas.drawPixel(cx, cy, BALLOON[i % 3]);
  }
}
