#pragma once
#include <Arduino.h>
#include "Animator.h"
#include "Pet.h"

// ============================================================
// FerretActor: the ferret's "brain". Picks the animation for the
// current situation:
//   - sleeping                      -> Sleep
//   - eating (after feed)           -> Dig, for ~1.8s
//   - jumping (pat or random)       -> Jump (one-shot)
//   - burrowing (random)            -> Dig -> Disappear -> Emerge
//   - otherwise                     -> wanders (walks <->) or stands
//                                      in Idle / Idle2 (random mix)
// Exposes the current frame + position for the Renderer to draw.
// ============================================================

class FerretActor {
 public:
  void begin();
  void onFeed();  // trigger the eating animation
  void onPat();   // trigger a jump (petting)
  void update(const Pet& pet, unsigned long now);

  const uint16_t* frame() const { return _anim.frame(); }
  int x() const { return (int)_x; }
  int y() const;
  int w() const;
  int h() const;
  uint16_t transparentKey() const;

 private:
  enum Mode { MODE_IDLE, MODE_IDLE2, MODE_WALK };
  enum Act  { ACT_NONE, ACT_EAT, ACT_JUMP, ACT_BURROW };

  Animator _anim;
  Mode _mode = MODE_IDLE;
  Act  _act = ACT_NONE;
  uint8_t _burrowPhase = 0;  // 0=dig, 1=disappear, 2=emerge

  float _x = 88.0f;          // top-left x (float for smooth motion)
  int _dir = 1;              // +1 right, -1 left
  bool _faceLeft = false;

  unsigned long _phaseUntil = 0;   // end of the current wander phase
  unsigned long _actUntil = 0;     // end of the current action/phase
  unsigned long _lastUpdate = 0;

  void pickWanderPhase(unsigned long now);
  void startJump(unsigned long now);
  void startBurrow(unsigned long now);
  void endAction(unsigned long now);
  void updateBurrow(unsigned long now);
};
