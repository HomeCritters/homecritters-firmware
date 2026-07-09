#include "InputController.h"
#include <Arduino.h>
#include "GameConfig.h"
#include "pins.h"

// Maps each bottom-arc button to its action.
static Action actionForButton(int idx) {
  switch (idx) {
    case ui::BTN_FEED:  return ACTION_FEED;
    case ui::BTN_PAT:   return ACTION_PAT;
    case ui::BTN_SLEEP: return ACTION_TOGGLE_SLEEP;
    case ui::BTN_CLEAN: return ACTION_CLEAN;
    default:            return ACTION_NONE;
  }
}

void InputController::begin() {
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

// Turns the end of a touch into an event: swipe (menu toggle) or tap.
InputEvent InputController::releaseEvent(bool menuOpen) {
  InputEvent ev;
  const int32_t dx = _lastX - _startX;
  const int32_t dy = _lastY - _startY;
  const int32_t adx = abs(dx), ady = abs(dy);

  // GESTURE: dominant, long swipe.
  if (ady > 45 && ady > adx) {  // vertical -> config menu
    if (dy > 0 && _startY < 120) { ev.ui = ui::UI_MENU_TOGGLE; return ev; }  // down -> open
    if (dy < 0 && menuOpen)      { ev.ui = ui::UI_MENU_TOGGLE; return ev; }  // up   -> close
  }
  if (adx > 45 && adx > ady) {  // horizontal -> games menu
    if (dx < 0 && _startX > 175) { ev.ui = ui::UI_GAMES_TOGGLE; return ev; } // left from right edge
  }

  // TAP: resolved at the touch start point.
  const int32_t px = _startX, py = _startY;
  if (menuOpen) { ev.ui = ui::menuHit(px, py); return ev; }
  if (ui::inHandle(px, py)) { ev.ui = ui::UI_MENU_TOGGLE; return ev; }
  if (ui::inRightHandle(px, py)) { ev.ui = ui::UI_GAMES_TOGGLE; return ev; }
  int idx = ui::buttonAt(px, py);
  if (idx >= 0) {
    ev.action = actionForButton(idx);
    ev.buttonIdx = idx;
  } else if (py >= ui::HANDLE_H && py < 160) {
    ev.action = ACTION_PAT;  // tapped the pet/scene
  }
  return ev;
}

InputEvent InputController::poll(LGFX_BallV2& lcd, bool menuOpen) {
  InputEvent ev;
  const unsigned long now = millis();

  // --- touch: track and act on RELEASE (enables swipe vs tap) ---
  int32_t tx, ty;
  if (lcd.getTouch(&tx, &ty)) {
    if (!_touching) { _touching = true; _startX = tx; _startY = ty; }
    _lastX = tx; _lastY = ty;
  } else if (_touching) {
    _touching = false;
    ev = releaseEvent(menuOpen);
    if (ev.action != ACTION_NONE || ev.ui != ui::UI_NONE) return ev;
  }

  // --- BOOT button: short press = feed, long press = sleep/wake ---
  const bool bootDown = (digitalRead(PIN_BOOT_BUTTON) == LOW);
  if (bootDown && !_bootWasDown) {
    _bootPressMs = now;
  }
  if (!bootDown && _bootWasDown) {
    const unsigned long heldMs = now - _bootPressMs;
    ev.action = (heldMs >= game::BOOT_LONGPRESS_MS) ? ACTION_TOGGLE_SLEEP
                                                    : ACTION_FEED;
  }
  _bootWasDown = bootDown;

  return ev;
}
