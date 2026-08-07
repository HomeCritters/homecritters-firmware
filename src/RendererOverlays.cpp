#include "Renderer.h"
#include "RendererShared.h"

// Renderer partial: full-screen overlays that ride ON TOP of the scene -
// party mode (disco ball/lasers/dance floor) and the Alexa-style voice ring.

using namespace theme;
using namespace ui;

// Checkered dance floor over the grass strip, drawn UNDER the pet so he
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

  // Two speaker cabinets flanking the stage (drawn under the pet: he can
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
  // under the pet so he dances in front of the fog).
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
