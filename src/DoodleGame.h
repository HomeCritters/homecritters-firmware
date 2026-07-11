#pragma once
#include <Arduino.h>

// ============================================================
// DoodleGame: a doodle-jump style mini-game. The ferret bounces
// upward off platforms automatically; the player only controls
// the horizontal position (follows the finger).
// Platform types: normal, spring (boost, jumps much higher),
// moving (slides sideways) and crumble (breaks after one bounce).
// Difficulty scales with height: wider gaps, faster movers, more
// crumbles. Best score persists in NVS.
// Pure logic/physics - the Renderer draws it. Screen is 240x240.
// ============================================================

class DoodleGame {
 public:
  static constexpr int PLAT_COUNT = 7;
  static constexpr int PLAT_W = 46;
  static constexpr int PLAT_H = 7;

  // vx != 0 -> slides sideways (bounces off the walls).
  // crumble -> breaks (disappears) right after the first bounce.
  struct Platform { float x, y, vx; bool active; bool spring; bool crumble; };

  void begin();  // load the high score from NVS
  void reset();
  // targetX: finger x in [0,240] to follow, or < 0 for no horizontal input.
  void update(unsigned long now, float targetX);

  bool gameOver() const { return _dead; }
  bool bounced() const { return _bounced; }  // true on the frame it bounced
  bool boosted() const { return _boosted; }  // that bounce was off a spring
  bool crumbled() const { return _crumbled; }  // that bounce broke the platform
  int score() const { return (int)(_climb / 10.0f); }
  int hiScore() const { return _hiScore; }
  bool newRecord() const { return _newRecord; }  // this run beat the record
  float climbPx() const { return _climb; }       // raw climb (parallax etc.)

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
  bool _boosted = false;
  bool _crumbled = false;
  int _hiScore = 0;
  bool _newRecord = false;
  unsigned long _lastUpdate = 0;

  float difficulty() const;  // 0..1 as the run gets higher
  void spawnPlatform(Platform& p, float x, float y, bool allowFeatures);
};
