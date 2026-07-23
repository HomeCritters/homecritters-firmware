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

  // Asleep - the Renderer dresses him (blanket + nightcap) only then.
  bool inBed() const { return _inBed; }

  const uint16_t* frame() const { return _anim.frame(); }
  int x() const { return (int)_x; }
  int y() const;
  int w() const;
  int h() const;
  uint16_t transparentKey() const;

  // Current animation (mirrored by the web portal): "idle","idle2","walk",
  // "jump","dig","disappear","emerge","sleep" + whether it faces left.
  const char* animName() const { return _animName; }
  bool faceLeft() const { return _faceLeft; }
  uint8_t frameIndex() const { return _anim.frameIndex(); }  // hat anchor lookup
  uint32_t animSeq() const { return _animSeq; }  // bumps on every animation change
  float xNorm() const;  // normalized horizontal position (0..1) for the portal

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
  bool _inBed = false;       // asleep at BED_X (see inBed())

  unsigned long _phaseUntil = 0;   // end of the current wander phase
  unsigned long _actUntil = 0;     // end of the current action/phase
  unsigned long _lastUpdate = 0;
  const char* _animName = "idle";
  uint32_t _animSeq = 0;

  // Switch the reported animation, counting changes (seq) so the web
  // portal can restart its clip cleanly.
  void setAnim(const char* n) {
    if (n != _animName) _animSeq++;
    _animName = n;
  }

  void pickWanderPhase(unsigned long now);
  void startJump(unsigned long now);
  void startBurrow(unsigned long now);
  void endAction(unsigned long now);
  void updateBurrow(unsigned long now);
};
