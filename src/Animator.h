#pragma once
#include <Arduino.h>

// ============================================================
// Animator: steps through a frame sequence over time. Knows
// nothing about the game or the sprite arrays - it only advances
// the current clip's frame.
//
// An AnimClip points to a contiguous block of N frames (each
// `stride` RGB565 pixels). Frame i starts at base + i*stride.
// ============================================================

struct AnimClip {
  const uint16_t* base;   // first pixel of the first frame
  uint16_t stride;        // pixels per frame (W*H)
  uint8_t count;          // number of frames
  uint16_t frameMs;       // duration of each frame
};

class Animator {
 public:
  // Switch clips (restarts from frame 0 when the clip changes).
  void play(const AnimClip& clip) {
    if (_clip == &clip) return;
    _clip = &clip;
    _idx = 0;
    _last = millis();
  }

  void update(unsigned long now) {
    if (!_clip) return;
    if (now - _last >= _clip->frameMs) {
      _idx = (_idx + 1) % _clip->count;
      _last = now;
    }
  }

  const uint16_t* frame() const {
    if (!_clip) return nullptr;
    return _clip->base + (uint32_t)_idx * _clip->stride;
  }
  uint8_t frameIndex() const { return _idx; }  // accessory anchors (hats)

 private:
  const AnimClip* _clip = nullptr;
  uint8_t _idx = 0;
  unsigned long _last = 0;
};
