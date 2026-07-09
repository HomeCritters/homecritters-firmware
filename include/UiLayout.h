#pragma once
#include <cstdint>
// ============================================================
// UI geometry shared between drawing (Renderer) and touch
// (InputController), so hit-testing and rendering can NEVER get
// out of sync. Round 240x240 screen, center (120,120), radius 120.
//
// The 4 action buttons sit on an ARC near the bottom, inside the
// visible circle (a straight row would clip at the round corners).
// ============================================================

namespace ui {

constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 240;
constexpr int16_t CENTER_X = 120;
constexpr int16_t CENTER_Y = 120;

// Action buttons, left to right.
enum ButtonId { BTN_FEED = 0, BTN_PAT, BTN_SLEEP, BTN_CLEAN, BTN_COUNT };

struct ButtonSlot {
  int16_t cx;
  int16_t cy;
};

// Centers on a shallow, low arc so that (a) button + radius fits inside
// the 120px circle and (b) the strip above stays free for the stat bars.
constexpr ButtonSlot BUTTONS[BTN_COUNT] = {
  { 56, 185},  // BTN_FEED   (apple)
  { 97, 205},  // BTN_PAT    (paw)
  {143, 205},  // BTN_SLEEP  (moon)
  {184, 185},  // BTN_CLEAN  (water drop)
};

constexpr int16_t BUTTON_RADIUS = 16;       // visual radius
constexpr int16_t BUTTON_TOUCH_RADIUS = 22; // touch radius (slightly larger)

// Returns the index of the tapped button, or -1 if none was hit.
inline int buttonAt(int32_t tx, int32_t ty) {
  for (int i = 0; i < BTN_COUNT; i++) {
    int32_t dx = tx - BUTTONS[i].cx;
    int32_t dy = ty - BUTTONS[i].cy;
    if (dx * dx + dy * dy <= BUTTON_TOUCH_RADIUS * BUTTON_TOUCH_RADIUS) {
      return i;
    }
  }
  return -1;
}

// ------------------- Config menu (full screen) -------------------
// Opens with a swipe down (or a tap on the top handle). Takes over the
// whole screen (not a modal). Closes with a swipe up or the Close button.
enum UiHit { UI_NONE, UI_MENU_TOGGLE, UI_VOL_DOWN, UI_VOL_UP, UI_WIFI, UI_GAMES_TOGGLE };

constexpr int16_t HANDLE_CX = 120, HANDLE_TOP = 0, HANDLE_W = 54, HANDLE_H = 16;

// Right-edge handle to open the games menu (swipe left from the edge, or tap).
constexpr int16_t RHANDLE_W = 14, RHANDLE_H = 54, RHANDLE_CY = 120;
inline bool inRightHandle(int32_t tx, int32_t ty) {
  return tx > SCREEN_W - RHANDLE_W - 2 &&
         ty > RHANDLE_CY - RHANDLE_H / 2 && ty < RHANDLE_CY + RHANDLE_H / 2;
}

// Volume: big buttons on the sides, track in the middle.
constexpr ButtonSlot MENU_VOL_MINUS = {40, 64};
constexpr ButtonSlot MENU_VOL_PLUS  = {200, 64};
constexpr int16_t MENU_BTN_R = 17;
constexpr int16_t MENU_QR_TOP = 106;  // top of the QR code (when connected)

// Bottom buttons: WiFi (setup) and Close.
constexpr int16_t MENU_WIFI_X = 44,  MENU_WIFI_Y = 182, MENU_WIFI_W = 72, MENU_WIFI_H = 32;
constexpr int16_t MENU_CLOSE_X = 124, MENU_CLOSE_Y = 182, MENU_CLOSE_W = 72, MENU_CLOSE_H = 32;

// WiFi setup screen (captive portal active): Exit button.
constexpr int16_t WIFI_EXIT_X = 70, WIFI_EXIT_Y = 190, WIFI_EXIT_W = 100, WIFI_EXIT_H = 34;

inline bool inHandle(int32_t tx, int32_t ty) {
  return tx > HANDLE_CX - HANDLE_W / 2 && tx < HANDLE_CX + HANDLE_W / 2 &&
         ty >= HANDLE_TOP && ty < HANDLE_TOP + HANDLE_H;
}

inline bool inCircle(int32_t tx, int32_t ty, const ButtonSlot& c, int r) {
  int32_t dx = tx - c.cx, dy = ty - c.cy;
  return dx * dx + dy * dy <= r * r;
}

inline bool inRect(int32_t tx, int32_t ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

// Hit-test while the menu is OPEN.
inline UiHit menuHit(int32_t tx, int32_t ty) {
  if (inHandle(tx, ty)) return UI_MENU_TOGGLE;
  if (inCircle(tx, ty, MENU_VOL_MINUS, MENU_BTN_R + 8)) return UI_VOL_DOWN;
  if (inCircle(tx, ty, MENU_VOL_PLUS, MENU_BTN_R + 8)) return UI_VOL_UP;
  if (inRect(tx, ty, MENU_WIFI_X, MENU_WIFI_Y, MENU_WIFI_W, MENU_WIFI_H)) return UI_WIFI;
  if (inRect(tx, ty, MENU_CLOSE_X, MENU_CLOSE_Y, MENU_CLOSE_W, MENU_CLOSE_H)) return UI_MENU_TOGGLE;
  return UI_NONE;
}

inline bool inWifiExit(int32_t tx, int32_t ty) {
  return inRect(tx, ty, WIFI_EXIT_X, WIFI_EXIT_Y, WIFI_EXIT_W, WIFI_EXIT_H);
}

// ------------------- Games menu (full screen) -------------------
constexpr int16_t GAME_BTN_X = 44, GAME_BTN_W = 152, GAME_BTN_H = 42;
constexpr int16_t GAME_DOODLE_Y = 76, GAME_BALL_Y = 126;
constexpr int16_t GAMES_BACK_X = 70, GAMES_BACK_Y = 188, GAMES_BACK_W = 100, GAMES_BACK_H = 34;

inline bool inGameDoodle(int32_t tx, int32_t ty) {
  return inRect(tx, ty, GAME_BTN_X, GAME_DOODLE_Y, GAME_BTN_W, GAME_BTN_H);
}
inline bool inGamesBack(int32_t tx, int32_t ty) {
  return inRect(tx, ty, GAMES_BACK_X, GAMES_BACK_Y, GAMES_BACK_W, GAMES_BACK_H);
}

// In a game: small back button in the top-left corner (rest of the screen
// controls the character).
inline bool inGameBack(int32_t tx, int32_t ty) { return tx < 42 && ty < 32; }

}  // namespace ui
