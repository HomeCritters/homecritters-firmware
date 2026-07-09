#pragma once
#include <Arduino.h>

// ============================================================
// DoodleGame: a doodle-jump style mini-game. The ferret bounces
// upward off platforms automatically; the player only controls
// the horizontal position (follows the finger). Some platforms
// have a spring that launches the ferret much higher (a boost).
// Pure logic/physics - the Renderer draws it. Screen is 240x240.
// ============================================================

class DoodleGame {
 public:
  static constexpr int PLAT_COUNT = 7;
  static constexpr int PLAT_W = 46;
  static constexpr int PLAT_H = 7;

  struct Platform { float x, y; bool active; bool spring; };

  void reset();
  // targetX: finger x in [0,240] to follow, or < 0 for no horizontal input.
  void update(unsigned long now, float targetX);

  bool gameOver() const { return _dead; }
  bool bounced() const { return _bounced; }  // true on the frame it bounced
  int score() const { return (int)(_climb / 10.0f); }

  float ferretX() const { return _fx; }  // center x
  float ferretY() const { return _fy; }  // top y
  bool faceLeft() const { return _faceLeft; }
  const Platform* platforms() const { return _plats; }

 private:
  float _fx = 120, _fy = 150, _vy = 0;
  float _climb = 0;  // total scrolled distance (score source)
  bool _faceLeft = false;
  Platform _plats[PLAT_COUNT];
  bool _dead = false;
  bool _bounced = false;
  unsigned long _lastUpdate = 0;

  void spawnPlatform(Platform& p, float x, float y, bool allowSpring);
};
