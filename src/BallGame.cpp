#include "BallGame.h"
#include <math.h>
#include <esp_random.h>

static constexpr float G = 900.0f;         // gravity (px/s^2)
static constexpr float REST_X = 120.0f;    // ball rest: centered, low (HUD area)
static constexpr float REST_Y = 176.0f;    // below the ferret's floor: launch pad only
static constexpr float GROUND_BALL = 128.0f;  // in flight the ball lands on the ferret's floor
static constexpr float WALL_L = 20.0f, WALL_R = 220.0f;  // inset from the round edge
static constexpr float BALL_R = 8.0f;
static constexpr float REST_WALL = 0.7f;   // wall bounce restitution
static constexpr float REST_GROUND = 0.5f;
static constexpr float FRICTION = 220.0f;  // rolling deceleration (px/s^2)
static constexpr float WANDER_MIN = 34.0f, WANDER_MAX = 206.0f;  // stroll range
static constexpr float CATCH_X = 28.0f;    // horizontal pickup reach
// Min launch speed so the ball ALWAYS rises above the ferret's floor (128) from
// the rest spot (176): needs ~sqrt(2*G*48) ~= 294 px/s, use a safe margin.
static constexpr float MIN_LAUNCH_VY = -360.0f;

void BallGame::reset() {
  _state = READY;
  _bx = REST_X; _by = REST_Y; _bvx = 0; _bvy = 0;
  _fx = 90; _faceLeft = false; _walking = false;
  _caught = false; _catches = 0;
  _wanderTarget = _fx; _wanderUntil = 0;
  _lastUpdate = millis();
}

void BallGame::throwBall(float vx, float vy) {
  if (_state != READY) return;
  _state = FLIGHT;  // launches from the rest spot, below the ferret
  _bvx = vx;
  _bvy = vy < MIN_LAUNCH_VY ? vy : MIN_LAUNCH_VY;  // always clears his floor
}

bool BallGame::takeCaught() {
  const bool c = _caught;
  _caught = false;
  return c;
}

void BallGame::moveFerretToward(float target, float dt, float speed) {
  const float dx = target - _fx;
  _walking = fabsf(dx) > 3.0f;
  if (!_walking) return;
  _faceLeft = dx < 0;
  const float step = speed * dt;
  _fx += fabsf(dx) < step ? dx : (dx < 0 ? -step : step);
}

// Idle stroll: walk to a random spot, pause a bit, pick a new one (like the
// pet-care scene). Slower than the chase, so it reads as "just hanging out".
void BallGame::wander(unsigned long now, float dt) {
  if (fabsf(_fx - _wanderTarget) < 4.0f) {  // arrived
    if (_wanderUntil == 0) {
      _wanderUntil = now + 500 + (esp_random() % 1600);  // pause here a while
    } else if (now >= _wanderUntil) {
      _wanderTarget = WANDER_MIN + (esp_random() % (int)(WANDER_MAX - WANDER_MIN));
      _wanderUntil = 0;
    }
    _walking = false;  // idling in place
    return;
  }
  moveFerretToward(_wanderTarget, dt, 42.0f);
}

void BallGame::update(unsigned long now) {
  float dt = (now - _lastUpdate) / 1000.0f;
  _lastUpdate = now;
  if (dt > 0.05f) dt = 0.05f;  // clamp after long pauses
  _walking = false;

  if (_state == CELEBRATE) {
    if (now >= _celebrateUntil) {  // done: ball back at the rest spot
      _state = READY;
      _bx = REST_X; _by = REST_Y; _bvx = 0; _bvy = 0;
      _wanderTarget = _fx; _wanderUntil = 0;  // resume strolling from here
    }
    return;
  }

  if (_state == READY) {
    wander(now, dt);                             // stroll while waiting
    _bx = REST_X;                                // ball fixed, centered, low
    _by = REST_Y + 4.0f * sinf(now * 0.005f);    // gentle bob
    return;
  }

  // FLIGHT: ball physics
  _bvy += G * dt;
  _bx += _bvx * dt;
  _by += _bvy * dt;

  // walls (inset from the round edge)
  if (_bx < WALL_L)  { _bx = WALL_L;  _bvx = -_bvx * REST_WALL; }
  if (_bx > WALL_R)  { _bx = WALL_R;  _bvx = -_bvx * REST_WALL; }

  // ground = the ferret's floor. Only collide while DESCENDING, so the ball
  // launches up from the rest spot (below the floor) without bouncing there;
  // it lands back on the floor and never sinks below it again.
  bool onGround = false;
  if (_bvy >= 0.0f && _by >= GROUND_BALL) {
    _by = GROUND_BALL;
    if (_bvy > 60.0f) {
      _bvy = -_bvy * REST_GROUND;
    } else {
      _bvy = 0;
      onGround = true;
      const float dec = FRICTION * dt;
      if (fabsf(_bvx) < dec) _bvx = 0;
      else _bvx += _bvx < 0 ? dec : -dec;
    }
  }

  // The ferret chases the ball's x (it stays on the home line). It only picks
  // the ball up once it has come back DOWN and slowed on the ground - never in
  // mid-air, so a straight-up throw isn't caught at the launch.
  moveFerretToward(_bx, dt);
  if (onGround && fabsf(_bvx) < 55.0f && fabsf(_fx - _bx) < CATCH_X) {
    _state = CELEBRATE;
    _celebrateUntil = now + 800;
    _catches++;
    _caught = true;
  }
}
