#include "InputController.h"
#include <Arduino.h>
#include "GameConfig.h"
#include "pins.h"
#include "TouchInput.h"

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
InputEvent InputController::releaseEvent(bool menuOpen, ui::MenuPage page) {
  InputEvent ev;
  const int32_t dx = _lastX - _startX;
  const int32_t dy = _lastY - _startY;
  const int32_t adx = abs(dx), ady = abs(dy);

  // GESTURE: dominant, long swipe.
  if (ady > ui::SWIPE_MIN && ady > adx) {  // vertical -> config menu / weather
    if (dy > 0 && _startY < 120) { ev.ui = ui::UI_MENU_TOGGLE; return ev; }  // down -> open
    if (dy < 0 && menuOpen)      { ev.ui = ui::UI_MENU_TOGGLE; return ev; }  // up   -> close
    // Swipe up starting on the bottom half (menu closed) -> weather forecast.
    if (dy < 0 && !menuOpen && _startY > 140) { ev.ui = ui::UI_WEATHER_TOGGLE; return ev; }
  }
  if (adx > ui::SWIPE_MIN && adx > ady) {  // horizontal
    // Right edge, pull left -> open the games menu (pet screen only).
    if (dx < 0 && _startX > ui::EDGE_RIGHT && !menuOpen) { ev.ui = ui::UI_GAMES_TOGGLE; return ev; }
    // Left edge, pull right -> "back" in menus; on the pet screen, open the
    // Home Assistant panel (mirror of the games pull on the right). The zone
    // is generous because the round panel's extreme-left touch is weak, so a
    // rightward swipe often first registers a bit inward.
    if (dx > 0 && _startX < ui::EDGE_LEFT_WIDE) {
      ev.ui = menuOpen ? ui::UI_MENU_BACK : ui::UI_HA_TOGGLE;
      return ev;
    }
  }

  // TAP: resolved at the touch start point.
  const int32_t px = _startX, py = _startY;
  if (menuOpen) { ev.ui = ui::menuHit(page, px, py); return ev; }
  if (ui::inHandle(px, py)) { ev.ui = ui::UI_MENU_TOGGLE; return ev; }
  if (ui::inRightHandle(px, py)) { ev.ui = ui::UI_GAMES_TOGGLE; return ev; }
  if (ui::inLeftHandle(px, py)) { ev.ui = ui::UI_HA_TOGGLE; return ev; }
  if (ui::inBottomHandle(px, py)) { ev.ui = ui::UI_WEATHER_TOGGLE; return ev; }
  int idx = ui::buttonAt(px, py);
  if (idx >= 0) {
    ev.action = actionForButton(idx);
    ev.buttonIdx = idx;
  } else if (py >= ui::HANDLE_H && py < 160) {
    ev.action = ACTION_PAT;  // tapped the pet/scene
  }
  return ev;
}

InputEvent InputController::poll(LGFX_BallV2& lcd, bool menuOpen, ui::MenuPage page) {
  InputEvent ev;
  const unsigned long now = millis();

  // --- touch: track and act on RELEASE (enables swipe vs tap) ---
  int32_t tx, ty;
  if (touchinput::read(lcd, &tx, &ty)) {  // physical OR portal-injected touch
    if (!_touching) { _touching = true; _startX = tx; _startY = ty; }
    _lastX = tx; _lastY = ty;
  } else if (_touching) {
    _touching = false;
    ev = releaseEvent(menuOpen, page);
    if (ev.action != ACTION_NONE || ev.ui != ui::UI_NONE) return ev;
  }

  // --- BOOT button: hold = push-to-talk, quick tap = mic mute toggle ---
  // Held past PTT_HOLD_MS starts a voice turn (mic streams to HA); a quick
  // press-release toggles the mic mute (privacy, like an Echo's mute button).
  // Feeding lives on the on-screen button + portal + HA.
  const bool bootDown = (digitalRead(PIN_BOOT_BUTTON) == LOW);
  if (bootDown && !_bootWasDown) {
    _bootPressMs = now;
  }
  if (bootDown && !_pttActive && (now - _bootPressMs) >= game::PTT_HOLD_MS) {
    _pttActive = true;
    ev.voice = InputEvent::V_PTT_START;  // held long enough: start talking
  }
  if (!bootDown && _bootWasDown) {
    if (_pttActive) { _pttActive = false; ev.voice = InputEvent::V_PTT_END; }
    else            { ev.voice = InputEvent::V_MUTE_TOGGLE; }  // quick tap
  }
  _bootWasDown = bootDown;

  return ev;
}
