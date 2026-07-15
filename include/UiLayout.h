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
// Opens with a swipe down (or a tap on the top handle). Takes over the whole
// screen (not a modal). Two levels of nesting:
//   MAIN (2x2): Audio | Luz | Conexao | Seguranca
//   CONN:       WiFi (config portal) | Portal (-> QR page)
//   SEC:        Aparelhos (paired clients) | Parear (starts a PIN pairing)
// The left-edge tab is "back" one level everywhere (menuParent()).
enum MenuPage {
  PAGE_MAIN, PAGE_AUDIO, PAGE_LIGHT,
  PAGE_CONN, PAGE_QR,       // QR ("Portal") nests under CONN
  PAGE_SEC, PAGE_SEC_HA,    // devices list nests under SEC
};

// Where "back" lands from each page (MAIN = will close the menu).
inline MenuPage menuParent(MenuPage p) {
  switch (p) {
    case PAGE_QR:     return PAGE_CONN;
    case PAGE_SEC_HA: return PAGE_SEC;
    default:          return PAGE_MAIN;
  }
}

enum UiHit {
  UI_NONE, UI_MENU_TOGGLE, UI_MENU_BACK,
  UI_OPEN_AUDIO, UI_OPEN_LIGHT, UI_OPEN_QR,
  UI_OPEN_CONN, UI_OPEN_SEC, UI_OPEN_HA_INFO, UI_PAIR, UI_REVOKE,
  UI_VOL_DOWN, UI_VOL_UP,
  UI_LED_DOWN, UI_LED_UP,
  UI_SCR_DOWN, UI_SCR_UP,
  UI_WIFI, UI_GAMES_TOGGLE
};

// "Revogar acesso" button on the Dispositivos page (bottom, inside the circle).
constexpr int16_t REVOKE_X = 50, REVOKE_Y = 172, REVOKE_W = 140, REVOKE_H = 30;
// "Voltar" button on the pairing overlay (bottom, inside the circle).
constexpr int16_t PAIR_CANCEL_X = 70, PAIR_CANCEL_Y = 188;
constexpr int16_t PAIR_CANCEL_W = 100, PAIR_CANCEL_H = 28;
inline bool inPairCancel(int32_t tx, int32_t ty) {
  // Forgiving 8px margin (inRect is declared further down in this header).
  return tx >= PAIR_CANCEL_X - 8 && tx <= PAIR_CANCEL_X + PAIR_CANCEL_W + 8 &&
         ty >= PAIR_CANCEL_Y - 8 && ty <= PAIR_CANCEL_Y + PAIR_CANCEL_H + 8;
}

constexpr int16_t HANDLE_CX = 120, HANDLE_TOP = 0, HANDLE_W = 54, HANDLE_H = 16;

// Right-edge handle to open the games menu (swipe left from the edge, or tap).
constexpr int16_t RHANDLE_W = 14, RHANDLE_H = 54, RHANDLE_CY = 120;
inline bool inRightHandle(int32_t tx, int32_t ty) {
  return tx > SCREEN_W - RHANDLE_W - 2 &&
         ty > RHANDLE_CY - RHANDLE_H / 2 && ty < RHANDLE_CY + RHANDLE_H / 2;
}

// Left-edge handle = "back" (config menu, games menu, Bolinha). Pull it toward
// the center (swipe right) or tap it. Same geometry, mirrored.
inline bool inLeftHandle(int32_t tx, int32_t ty) {
  return tx < RHANDLE_W + 2 &&
         ty > RHANDLE_CY - RHANDLE_H / 2 && ty < RHANDLE_CY + RHANDLE_H / 2;
}

constexpr int16_t MENU_BTN_R = 17;  // +/- stepper button radius

// Main page: a perfect 2x2 grid of equal squares - Audio, Luz (top row),
// WiFi, QR (bottom row). All four are the same size; the QR tile is tappable
// and opens the QR detail page.
constexpr int16_t MENU_CELL_W = 70, MENU_CELL_H = 66;
constexpr int16_t MENU_COL_L = 43, MENU_COL_R = 127;   // column x
constexpr int16_t MENU_ROW_1 = 48, MENU_ROW_2 = 118;   // row y (battery pill above)
constexpr int16_t MENU_QR_CX  = MENU_COL_R + MENU_CELL_W / 2;  // QR center x
// Sub-pages with two tiles (Conexao, Seguranca): one centered row.
constexpr int16_t MENU_SUB_ROW = 86;

// Audio sub-page: one volume stepper (centered).
constexpr ButtonSlot MENU_VOL_MINUS = {40, 100};
constexpr ButtonSlot MENU_VOL_PLUS  = {200, 100};

// Light sub-page: two steppers (LED brightness + screen brightness).
constexpr ButtonSlot MENU_LED_MINUS = {40, 72};
constexpr ButtonSlot MENU_LED_PLUS  = {200, 72};
constexpr ButtonSlot MENU_SCR_MINUS = {40, 140};
constexpr ButtonSlot MENU_SCR_PLUS  = {200, 140};

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

// Hit-test while the menu is OPEN (page-aware). The left-edge tab is "back"
// (UI_MENU_BACK) on every page; main resolves it (sub-page -> main -> closed).
inline UiHit menuHit(MenuPage page, int32_t tx, int32_t ty) {
  if (inHandle(tx, ty)) return UI_MENU_TOGGLE;
  if (inLeftHandle(tx, ty)) return UI_MENU_BACK;
  if (page == PAGE_AUDIO) {
    if (inCircle(tx, ty, MENU_VOL_MINUS, MENU_BTN_R + 8)) return UI_VOL_DOWN;
    if (inCircle(tx, ty, MENU_VOL_PLUS, MENU_BTN_R + 8)) return UI_VOL_UP;
    return UI_NONE;
  }
  if (page == PAGE_LIGHT) {
    if (inCircle(tx, ty, MENU_LED_MINUS, MENU_BTN_R + 8)) return UI_LED_DOWN;
    if (inCircle(tx, ty, MENU_LED_PLUS, MENU_BTN_R + 8)) return UI_LED_UP;
    if (inCircle(tx, ty, MENU_SCR_MINUS, MENU_BTN_R + 8)) return UI_SCR_DOWN;
    if (inCircle(tx, ty, MENU_SCR_PLUS, MENU_BTN_R + 8)) return UI_SCR_UP;
    return UI_NONE;
  }
  if (page == PAGE_CONN) {  // WiFi | Portal (QR)
    if (inRect(tx, ty, MENU_COL_L, MENU_SUB_ROW, MENU_CELL_W, MENU_CELL_H)) return UI_WIFI;
    if (inRect(tx, ty, MENU_COL_R, MENU_SUB_ROW, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_QR;
    return UI_NONE;
  }
  if (page == PAGE_SEC) {  // Aparelhos | Parear (PIN)
    if (inRect(tx, ty, MENU_COL_L, MENU_SUB_ROW, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_HA_INFO;
    if (inRect(tx, ty, MENU_COL_R, MENU_SUB_ROW, MENU_CELL_W, MENU_CELL_H)) return UI_PAIR;
    return UI_NONE;
  }
  if (page == PAGE_SEC_HA) {  // Dispositivos: revoke-all button
    if (inRect(tx, ty, REVOKE_X, REVOKE_Y, REVOKE_W, REVOKE_H)) return UI_REVOKE;
    return UI_NONE;
  }
  if (page == PAGE_QR) return UI_NONE;
  // PAGE_MAIN: 2x2 grid.
  if (inRect(tx, ty, MENU_COL_L, MENU_ROW_1, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_AUDIO;
  if (inRect(tx, ty, MENU_COL_R, MENU_ROW_1, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_LIGHT;
  if (inRect(tx, ty, MENU_COL_L, MENU_ROW_2, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_CONN;
  if (inRect(tx, ty, MENU_COL_R, MENU_ROW_2, MENU_CELL_W, MENU_CELL_H)) return UI_OPEN_SEC;
  return UI_NONE;
}

// ------------------- Games menu (full screen) -------------------
// Three tiles: Jump! + Bolinha on top, Genius centered below. Same tile size
// and columns as the config menu (owner request: consistent buttons).
constexpr int16_t GAME_TILE_W = MENU_CELL_W, GAME_TILE_H = MENU_CELL_H;
constexpr int16_t GAME_ROW_1 = 48, GAME_ROW_2 = 124;
constexpr int16_t GAME_COL_L = MENU_COL_L, GAME_COL_R = MENU_COL_R;
constexpr int16_t GAME_COL_C = (SCREEN_W - GAME_TILE_W) / 2;

inline bool inGameDoodle(int32_t tx, int32_t ty) {
  return inRect(tx, ty, GAME_COL_L, GAME_ROW_1, GAME_TILE_W, GAME_TILE_H);
}
inline bool inGameBall(int32_t tx, int32_t ty) {
  return inRect(tx, ty, GAME_COL_R, GAME_ROW_1, GAME_TILE_W, GAME_TILE_H);
}
inline bool inGameSimon(int32_t tx, int32_t ty) {
  return inRect(tx, ty, GAME_COL_C, GAME_ROW_2, GAME_TILE_W, GAME_TILE_H);
}

// ------------------- Genius / Simon (full screen) -------------------
// Four diagonal quadrant arcs around the edge (a color ring); the center
// holds the score, the ferret and the exit button.
constexpr int16_t SIMON_RING_INNER = 80;   // ring inner radius (draw)
constexpr int16_t SIMON_RING_OUTER = 118;  // ring outer radius (draw)
constexpr int16_t SIMON_EXIT_CY = 182;     // exit button center y (bottom gap)

// Which color pad is at (tx,ty): 0=TL green, 1=TR red, 2=BL yellow,
// 3=BR blue, -1 = center area (not a pad). Forgiving on the outer side.
inline int simonColorAt(int32_t tx, int32_t ty) {
  const int32_t dx = tx - CENTER_X, dy = ty - CENTER_Y;
  if (dx * dx + dy * dy < 76 * 76) return -1;  // center: ferret/score/exit
  if (dx < 0 && dy < 0) return 0;
  if (dx >= 0 && dy < 0) return 1;
  if (dx < 0) return 2;
  return 3;
}
inline bool inSimonExit(int32_t tx, int32_t ty) {
  const int32_t dx = tx - CENTER_X, dy = ty - SIMON_EXIT_CY;
  return dx * dx + dy * dy <= 20 * 20;
}

// In a game: small back button in the top-left corner (rest of the screen
// controls the character).
inline bool inGameBack(int32_t tx, int32_t ty) { return tx < 42 && ty < 32; }

}  // namespace ui
