#pragma once
#include <Arduino.h>

// ============================================================
// BallGame ("Bolinha"): a fetch mini-game. The player swipes to
// throw a tennis ball; it flies with gravity, bounces off the
// walls/ground and rolls to a stop. The ferret chases it, and
// when it reaches the stopped ball it "catches" it, celebrates
// (jump animation) and the ball comes back for the next throw.
// Pure logic/physics - the Renderer draws it. Screen is 240x240.
// ============================================================

class BallGame {
 public:
  enum State { READY, FLIGHT, CELEBRATE };

  void reset();
  void update(unsigned long now);
  // Launch (only while READY). vx/vy in px/s; vy must be negative (upward).
  void throwBall(float vx, float vy);
  bool takeCaught();  // event: true once right after a catch (for the sound)

  State state() const { return _state; }
  bool ready() const { return _state == READY; }
  int catches() const { return _catches; }

  float ballX() const { return _bx; }
  float ballY() const { return _by; }
  float ferretX() const { return _fx; }
  bool faceLeft() const { return _faceLeft; }
  bool walking() const { return _walking; }  // ferret moved this frame

 private:
  State _state = READY;
  float _bx = 120, _by = 150, _bvx = 0, _bvy = 0;  // ball center + velocity
  float _fx = 60;                                  // ferret center x (on ground)
  bool _faceLeft = false;
  bool _walking = false;
  bool _caught = false;
  int _catches = 0;
  unsigned long _celebrateUntil = 0, _lastUpdate = 0;

  // Idle wander (while waiting for a throw): stroll to a target, then pause.
  float _wanderTarget = 60;
  unsigned long _wanderUntil = 0;

  void moveFerretToward(float target, float dt, float speed = 95.0f);
  void wander(unsigned long now, float dt);
};
