#include "DoodleGame.h"
#include <Preferences.h>
#include <esp_random.h>

static constexpr float G = 780.0f;         // gravity (px/s^2)
static constexpr float JUMP_VY = -370.0f;  // normal bounce impulse (px/s)
static constexpr float SPRING_VY = -650.0f;// spring boost impulse (px/s)
static constexpr float SCROLL_LINE = 96.0f;// climbing above this scrolls the world
static constexpr float GAP = 46.0f;        // vertical gap between platforms (base)
static constexpr float TRACK = 20.0f;      // finger tracking speed (higher = snappier)
static constexpr int   FEET = 26;          // ferret height (top y -> feet)
static constexpr int   FHW = 11;           // ferret half-width
static constexpr float MOVE_SPEED = 55.0f; // sliding-platform speed at diff 0 (px/s)
static constexpr float DIFF_CLIMB = 3000.0f;  // climb (px) to reach max difficulty

static float randPlatformX() {
  return 6 + (esp_random() % (240 - DoodleGame::PLAT_W - 12));
}

void DoodleGame::begin() {
  Preferences p;
  p.begin("game", true);
  _hiScore = p.getInt("hs", 0);
  p.end();
}

// Ramps 0 -> 1 over the first DIFF_CLIMB pixels of climb.
float DoodleGame::difficulty() const {
  const float d = _climb / DIFF_CLIMB;
  return d > 1.0f ? 1.0f : d;
}

void DoodleGame::spawnPlatform(Platform& p, float x, float y, bool allowFeatures) {
  p.x = x;
  p.y = y;
  p.vx = 0;
  p.active = true;
  p.spring = false;
  p.crumble = false;
  if (!allowFeatures) return;

  const float d = difficulty();
  // Sequential rolls keep the types mutually exclusive.
  if ((esp_random() % 100) < 18) {
    p.spring = true;  // springs stay put (a moving spring is too chaotic)
  } else if ((int)(esp_random() % 100) < 10 + (int)(15 * d)) {
    p.crumble = true;  // 10% -> 25% as the run climbs
  } else if ((int)(esp_random() % 100) < 22 + (int)(10 * d)) {
    p.vx = ((esp_random() & 1) ? 1 : -1) * (MOVE_SPEED + 45.0f * d);
  }
}

void DoodleGame::reset() {
  _fx = 120; _fy = 150; _vy = JUMP_VY;
  _climb = 0; _dead = false; _bounced = false; _boosted = false; _crumbled = false;
  _newRecord = false; _faceLeft = false;
  _lastUpdate = millis();
  spawnPlatform(_plats[0], 100, 200, false);  // starting platform (plain)
  for (int i = 1; i < PLAT_COUNT; i++) {
    spawnPlatform(_plats[i], randPlatformX(), 200 - i * GAP, true);
  }
}

void DoodleGame::update(unsigned long now, float targetX) {
  _bounced = false;
  _crumbled = false;
  if (_dead) return;
  float dt = (now - _lastUpdate) / 1000.0f;
  _lastUpdate = now;
  if (dt > 0.05f) dt = 0.05f;  // clamp after long pauses

  // slide moving platforms; bounce them off the walls
  for (auto& p : _plats) {
    if (!p.active || p.vx == 0) continue;
    p.x += p.vx * dt;
    if (p.x < 0)              { p.x = 0;              p.vx = -p.vx; }
    if (p.x > 240 - PLAT_W)   { p.x = 240 - PLAT_W;   p.vx = -p.vx; }
  }

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
        _boosted = p.spring;
        _crumbled = p.crumble;
        if (p.crumble) p.active = false;  // one bounce and it breaks away
        break;
      }
    }
  }

  // scroll the world down while climbing; recycle platforms off the bottom.
  // The vertical gap widens with difficulty (still below the ~87px jump reach).
  if (_fy < SCROLL_LINE) {
    const float shift = SCROLL_LINE - _fy;
    _fy = SCROLL_LINE;
    _climb += shift;
    const float gap = GAP + 20.0f * difficulty();
    float top = 9999;
    for (auto& p : _plats) { p.y += shift; if (p.y < top) top = p.y; }
    for (auto& p : _plats) {
      if (p.y > 240) spawnPlatform(p, randPlatformX(), top -= gap, true);
    }
  }

  if (_fy > 246) {  // fell off the bottom
    _dead = true;
    const int s = score();
    if (s > _hiScore) {  // persist the new record right away
      _hiScore = s;
      _newRecord = true;
      Preferences p;
      p.begin("game", false);
      p.putInt("hs", _hiScore);
      p.end();
    }
  }
}
