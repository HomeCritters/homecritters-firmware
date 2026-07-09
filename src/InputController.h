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
//   - short BOOT press            -> feed
//   - long BOOT press (>1.5s)     -> sleep/wake
// Gestures are resolved on touch RELEASE (swipe vs tap).
// ============================================================

struct InputEvent {
  Action action = ACTION_NONE;
  int buttonIdx = -1;          // -1 = no bottom-arc button
  ui::UiHit ui = ui::UI_NONE;  // menu/handle interaction
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

  // Touch tracking to tell a swipe from a tap on release.
  bool _touching = false;
  int32_t _startX = 0, _startY = 0;
  int32_t _lastX = 0, _lastY = 0;

  InputEvent releaseEvent(bool menuOpen, ui::MenuPage page);
};
