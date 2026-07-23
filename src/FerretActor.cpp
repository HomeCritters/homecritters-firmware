#include "FerretActor.h"
#include <esp_random.h>
#include "ferret_anim.h"  // sprite arrays (only included here)

// Animation clips (pointing into the generated PROGMEM blocks).
static const AnimClip CLIP_IDLE      = {&ferret_idle[0][0],      FERRET_FRAME_PIXELS, FERRET_IDLE_FRAMES,      160};
static const AnimClip CLIP_IDLE_L    = {&ferret_idle_l[0][0],    FERRET_FRAME_PIXELS, FERRET_IDLE_L_FRAMES,    160};
static const AnimClip CLIP_IDLE2     = {&ferret_idle2[0][0],     FERRET_FRAME_PIXELS, FERRET_IDLE2_FRAMES,     180};
static const AnimClip CLIP_IDLE2_L   = {&ferret_idle2_l[0][0],   FERRET_FRAME_PIXELS, FERRET_IDLE2_L_FRAMES,   180};
static const AnimClip CLIP_WALK      = {&ferret_walk[0][0],      FERRET_FRAME_PIXELS, FERRET_WALK_FRAMES,      90};
static const AnimClip CLIP_WALK_L    = {&ferret_walk_l[0][0],    FERRET_FRAME_PIXELS, FERRET_WALK_L_FRAMES,    90};
static const AnimClip CLIP_JUMP      = {&ferret_jump[0][0],      FERRET_FRAME_PIXELS, FERRET_JUMP_FRAMES,      70};
static const AnimClip CLIP_JUMP_L    = {&ferret_jump_l[0][0],    FERRET_FRAME_PIXELS, FERRET_JUMP_L_FRAMES,    70};
static const AnimClip CLIP_DIG       = {&ferret_dig[0][0],       FERRET_FRAME_PIXELS, FERRET_DIG_FRAMES,       90};
static const AnimClip CLIP_DISAPPEAR = {&ferret_disappear[0][0], FERRET_FRAME_PIXELS, FERRET_DISAPPEAR_FRAMES, 85};
static const AnimClip CLIP_EMERGE    = {&ferret_emerge[0][0],    FERRET_FRAME_PIXELS, FERRET_EMERGE_FRAMES,    85};
static const AnimClip CLIP_SLEEP     = {&ferret_sleep[0][0],     FERRET_FRAME_PIXELS, FERRET_SLEEP_FRAMES,     200};

// Layout: the ferret walks on the grass; feet at the bottom of the frame.
static constexpr int   FLOOR_Y = 128;
static constexpr float X_MIN = 12.0f;
static constexpr float X_MAX = 240.0f - FERRET_FRAME_W - 12.0f;
static constexpr float SPEED = 34.0f;                       // px/s (wander speed)
static constexpr int   X_REST = (240 - FERRET_FRAME_W) / 2;
static constexpr unsigned long EAT_MS = 1800;

// Duration of one full pass of a clip (for one-shot actions).
static unsigned long clipDuration(const AnimClip& c) { return (unsigned long)c.count * c.frameMs; }

int FerretActor::y() const { return FLOOR_Y - FERRET_FRAME_H; }
int FerretActor::w() const { return FERRET_FRAME_W; }
int FerretActor::h() const { return FERRET_FRAME_H; }
uint16_t FerretActor::transparentKey() const { return FERRET_TRANSPARENT_KEY; }

float FerretActor::xNorm() const {
  float t = (_x - X_MIN) / (X_MAX - X_MIN);
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

void FerretActor::begin() {
  randomSeed(esp_random());
  _x = X_REST;
  _lastUpdate = millis();
  pickWanderPhase(_lastUpdate);
}

void FerretActor::onFeed() {
  _act = ACT_EAT;
  _actUntil = millis() + EAT_MS;
}

void FerretActor::onPat() {
  if (_act == ACT_NONE || _act == ACT_JUMP) startJump(millis());
}

void FerretActor::startJump(unsigned long now) {
  _act = ACT_JUMP;
  _actUntil = now + clipDuration(CLIP_JUMP);
}

void FerretActor::startBurrow(unsigned long now) {
  _act = ACT_BURROW;
  _burrowPhase = 0;
  _actUntil = now + clipDuration(CLIP_DIG);
}

void FerretActor::endAction(unsigned long now) {
  _act = ACT_NONE;
  _mode = MODE_IDLE;
  _phaseUntil = now + random(1200, 2500);  // rest before the next roll
}

void FerretActor::pickWanderPhase(unsigned long now) {
  // Occasionally roll a special action between wander phases.
  int r = random(0, 100);
  if (r < 12) { startJump(now); return; }    // ~12% jump
  if (r < 22) { startBurrow(now); return; }  // ~10% burrow

  if (_mode == MODE_WALK) {
    // just walked -> now stand still, randomly mixing Idle / Idle2
    _mode = (random(0, 2) == 0) ? MODE_IDLE : MODE_IDLE2;
    _phaseUntil = now + random(1500, 3500);
  } else {
    // standing -> walk in some direction (biased away from the edges)
    _mode = MODE_WALK;
    if (_x < X_MIN + 20)      _dir = 1;
    else if (_x > X_MAX - 20) _dir = -1;
    else                      _dir = (random(0, 2) == 0) ? -1 : 1;
    _faceLeft = (_dir < 0);
    _phaseUntil = now + random(1200, 2600);
  }
}

void FerretActor::updateBurrow(unsigned long now) {
  if (_burrowPhase == 0) {
    _anim.play(CLIP_DIG);
    if (now >= _actUntil) { _burrowPhase = 1; _actUntil = now + clipDuration(CLIP_DISAPPEAR); }
  } else if (_burrowPhase == 1) {
    _anim.play(CLIP_DISAPPEAR);
    if (now >= _actUntil) { _burrowPhase = 2; _actUntil = now + clipDuration(CLIP_EMERGE); }
  } else {
    _anim.play(CLIP_EMERGE);
    if (now >= _actUntil) endAction(now);
  }
}

void FerretActor::update(const Pet& pet, unsigned long now) {
  float dt = (now - _lastUpdate) / 1000.0f;
  _lastUpdate = now;
  if (dt > 0.2f) dt = 0.2f;  // avoid "teleporting" after long pauses

  if (pet.sleeping()) {
    _act = ACT_NONE;
    // Sleepy ferret heads to his bed first (walks there like any wander),
    // then curls up on the mattress. Blanket/nightcap only once inBed.
    if (fabsf(_x - (float)BED_X) > 3.0f) {
      _inBed = false;
      _dir = _x < BED_X ? 1 : -1;
      _faceLeft = _dir < 0;
      _x += _dir * SPEED * dt;
      _anim.play(_faceLeft ? CLIP_WALK_L : CLIP_WALK);
      setAnim("walk");
    } else {
      _x = BED_X;
      _inBed = true;
      _faceLeft = false;
      _anim.play(CLIP_SLEEP);
      setAnim("sleep");
    }
    _anim.update(now);
    return;
  }
  _inBed = false;

  if (_act == ACT_EAT) {
    _anim.play(CLIP_DIG);
    setAnim("dig");
    if (now >= _actUntil) endAction(now);
  } else if (_act == ACT_JUMP) {
    _anim.play(_faceLeft ? CLIP_JUMP_L : CLIP_JUMP);
    setAnim("jump");
    if (now >= _actUntil) endAction(now);
  } else if (_act == ACT_BURROW) {
    updateBurrow(now);
    setAnim(_burrowPhase == 0 ? "dig" : (_burrowPhase == 1 ? "disappear" : "emerge"));
  } else {
    // idle wandering
    if (now >= _phaseUntil) pickWanderPhase(now);

    if (_mode == MODE_WALK) {
      _x += _dir * SPEED * dt;
      if (_x <= X_MIN) { _x = X_MIN; _dir = 1; _faceLeft = false; }
      if (_x >= X_MAX) { _x = X_MAX; _dir = -1; _faceLeft = true; }
      _anim.play(_faceLeft ? CLIP_WALK_L : CLIP_WALK);
      setAnim("walk");
    } else if (_mode == MODE_IDLE2) {
      _anim.play(_faceLeft ? CLIP_IDLE2_L : CLIP_IDLE2);
      setAnim("idle2");
    } else {
      _anim.play(_faceLeft ? CLIP_IDLE_L : CLIP_IDLE);
      setAnim("idle");
    }
  }

  _anim.update(now);
}
