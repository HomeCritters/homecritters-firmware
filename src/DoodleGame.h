#pragma once
#include <Arduino.h>

// ============================================================
// DoodleGame: a doodle-jump style mini-game. The ferret bounces
// upward off platforms automatically; the player only controls
// the horizontal movement. Pure logic/physics - the Renderer
// draws it from this state. Screen is 240x240 (see .cpp).
// ============================================================

class DoodleGame {
 public:
  static constexpr int PLAT_COUNT = 7;
  static constexpr int PLAT_W = 46;
  static constexpr int PLAT_H = 7;

  struct Platform { float x, y; bool active; };

  void reset();
  void update(unsigned long now, float control);  // control -1..1 (horizontal)

  bool gameOver() const { return _dead; }
  bool bounced() const { return _bounced; }  // true on the frame it bounced
  int score() const { return (int)(_climb / 10.0f); }

  float ferretX() const { return _fx; }  // center x
  float ferretY() const { return _fy; }  // top y
  bool faceLeft() const { return _vx < 0; }
  const Platform* platforms() const { return _plats; }

 private:
  float _fx = 120, _fy = 150, _vx = 0, _vy = 0;
  float _climb = 0;  // total scrolled distance (score source)
  Platform _plats[PLAT_COUNT];
  bool _dead = false;
  bool _bounced = false;
  unsigned long _lastUpdate = 0;
};
