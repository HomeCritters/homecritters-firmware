#pragma once
#include "LGFX_BallV2.h"
#include "UiLayout.h"
#include "Pet.h"

// ============================================================
// InputController: translates touch (CST816) and the BOOT button
// into Actions / UI events.
//   - tap on a bottom-arc icon    -> that button's action
//   - tap on the pet/scene        -> pet it (ACTION_PAT)
//   - swipe down / tap top handle -> toggle the config menu
//   - quick BOOT tap              -> toggle mic mute (privacy, Echo-style)
//   - BOOT hold (>=PTT_HOLD_MS)   -> push-to-talk (voice assistant)
// Gestures are resolved on touch RELEASE (swipe vs tap).
// ============================================================

struct InputEvent {
  enum Voice { V_NONE, V_PTT_START, V_PTT_END, V_MUTE_TOGGLE };
  Action action = ACTION_NONE;
  int buttonIdx = -1;          // -1 = no bottom-arc button
  ui::UiHit ui = ui::UI_NONE;  // menu/handle interaction
  Voice voice = V_NONE;        // BOOT push-to-talk edge
};

class InputController {
 public:
  void begin();
  // menuOpen: when true, touches are interpreted against the config menu;
  // page selects which config sub-page is showing.
  InputEvent poll(LGFX_BallV2& lcd, bool menuOpen, ui::MenuPage page);

 private:
  unsigned long _bootPressMs = 0;
  bool _bootWasDown = false;
  bool _pttActive = false;  // BOOT held past the talk threshold this press

  // Touch tracking to tell a swipe from a tap on release.
  bool _touching = false;
  int32_t _startX = 0, _startY = 0;
  int32_t _lastX = 0, _lastY = 0;

  InputEvent releaseEvent(bool menuOpen, ui::MenuPage page);
};
