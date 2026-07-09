#include "DoodleGame.h"
#include <esp_random.h>

static constexpr float G = 780.0f;         // gravity (px/s^2)
static constexpr float JUMP_VY = -370.0f;  // normal bounce impulse (px/s)
static constexpr float SPRING_VY = -650.0f;// spring boost impulse (px/s)
static constexpr float SCROLL_LINE = 96.0f;// climbing above this scrolls the world
static constexpr float GAP = 46.0f;        // vertical gap between platforms
static constexpr float TRACK = 20.0f;      // finger tracking speed (higher = snappier)
static constexpr int   FEET = 26;          // ferret height (top y -> feet)
static constexpr int   FHW = 11;           // ferret half-width

static float randPlatformX() {
  return 6 + (esp_random() % (240 - DoodleGame::PLAT_W - 12));
}

void DoodleGame::spawnPlatform(Platform& p, float x, float y, bool allowSpring) {
  p.x = x;
  p.y = y;
  p.active = true;
  p.spring = allowSpring && (esp_random() % 100) < 18;  // ~18% springs
}

void DoodleGame::reset() {
  _fx = 120; _fy = 150; _vy = JUMP_VY;
  _climb = 0; _dead = false; _bounced = false; _faceLeft = false;
  _lastUpdate = millis();
  spawnPlatform(_plats[0], 100, 200, false);  // starting platform (no spring)
  for (int i = 1; i < PLAT_COUNT; i++) {
    spawnPlatform(_plats[i], randPlatformX(), 200 - i * GAP, true);
  }
}

void DoodleGame::update(unsigned long now, float targetX) {
  _bounced = false;
  if (_dead) return;
  float dt = (now - _lastUpdate) / 1000.0f;
  _lastUpdate = now;
  if (dt > 0.05f) dt = 0.05f;  // clamp after long pauses

  // horizontal: follow the finger directly (snappy, no perceptible lag)
  if (targetX >= 0) {
    const float dx = targetX - _fx;
    if (dx < -1) _faceLeft = true;
    else if (dx > 1) _faceLeft = false;
    float k = TRACK * dt;
    if (k > 1) k = 1;
    _fx += dx * k;
  }
  if (_fx < FHW) _fx = FHW;
  if (_fx > 240 - FHW) _fx = 240 - FHW;

  // vertical
  const float prevFeet = _fy + FEET;
  _vy += G * dt;
  _fy += _vy * dt;
  const float feet = _fy + FEET;

  // bounce off a platform (only while falling, when the feet cross its top)
  if (_vy > 0) {
    for (auto& p : _plats) {
      if (!p.active) continue;
      if (prevFeet <= p.y && feet >= p.y &&
          _fx + FHW > p.x && _fx - FHW < p.x + PLAT_W) {
        _vy = p.spring ? SPRING_VY : JUMP_VY;
        _bounced = true;
        break;
      }
    }
  }

  // scroll the world down while climbing; recycle platforms off the bottom
  if (_fy < SCROLL_LINE) {
    const float shift = SCROLL_LINE - _fy;
    _fy = SCROLL_LINE;
    _climb += shift;
    float top = 9999;
    for (auto& p : _plats) { p.y += shift; if (p.y < top) top = p.y; }
    for (auto& p : _plats) {
      if (p.y > 240) spawnPlatform(p, randPlatformX(), top -= GAP, true);
    }
  }

  if (_fy > 246) _dead = true;  // fell off the bottom
}
