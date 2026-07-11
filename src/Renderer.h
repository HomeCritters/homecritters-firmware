#pragma once
#include "LGFX_BallV2.h"
#include "UiLayout.h"
#include "Theme.h"
#include "Pet.h"
#include "Battery.h"
#include "FerretActor.h"
#include "Clock.h"
#include "DoodleGame.h"
#include "BallGame.h"

// ============================================================
// Renderer: owns the display and the canvas (double buffer) and
// draws the whole scene. Knows no game rules - it only paints
// whatever Pet/Battery/FerretActor report.
// ============================================================

class Renderer {
 public:
  explicit Renderer(LGFX_BallV2& lcd) : _lcd(lcd), _canvas(&lcd) {}

  void begin();
  void draw(const Pet& pet, Battery& battery, FerretActor& ferret,
            bool menuOpen, ui::MenuPage menuPage, int volume, int ledBright,
            bool wifiOn, const char* ip, bool clockActive, Clock& clock);

  // Screen (backlight) brightness 0..100, persisted to NVS. The Renderer owns
  // the display, so it owns this too. Floored so the screen never goes dark.
  void setScreenBrightness(int pct);
  int screenBrightness() const { return _scrBright; }

  // Full-screen WiFi setup screen (captive portal active) with Exit button.
  void drawWifiConfig(const char* apName);

  // Game screens.
  void drawGamesMenu();
  void drawDoodle(DoodleGame& game);
  void drawBall(BallGame& game);

  // Visual feedback: highlight the tapped button for a few ms.
  void flashButton(int idx);

 private:
  LGFX_BallV2& _lcd;
  LGFX_Sprite  _canvas;

  int _pressedButton = -1;
  unsigned long _pressedUntil = 0;
  int _scrBright = 70;  // screen backlight brightness (0..100)

  // Palette active for the current frame (day or night). Set at the top
  // of draw() and read by the helpers, instead of threading a parameter.
  theme::ScenePalette _p = theme::NIGHT;

  void drawSky();
  void drawStars();
  void drawMoon();
  void drawSun();
  void drawSunset();
  void drawForest(bool night);   // treeline + cabin + grass + trees
  void drawCabin(int bx, int by, bool night);
  void drawPineTree(int bx, int baseY, int size);
  void drawSparkles(bool night);
  void drawHeader(const Pet& pet, bool wifiOn);
  void drawMenuHandle();
  void drawRightHandle();
  void drawClock(Clock& clock);
  void drawDoodleFerret(int cx, int cy, bool faceLeft);
  // mode: 0 = idle, 1 = walking, 2 = jumping (celebration). zoom scales the
  // 40px game sprite (2 -> 80px, matching the scene ferret in Bolinha).
  void drawGameFerret(int cx, int cy, int mode, bool faceLeft, int zoom = 1);
  void drawTennisBall(int cx, int cy, int r);
  void drawMenu(ui::MenuPage page, int volume, int ledBright, bool wifiOn, const char* ip);
  void drawMenuMain(bool wifiOn, const char* ip);
  void drawMenuAudio(int volume);
  void drawMenuLight(int ledBright);
  void drawMenuQr(bool wifiOn, const char* ip);
  void drawStepper(const char* label, int pct, const ui::ButtonSlot& minus,
                   const ui::ButtonSlot& plus);
  void drawGridCell(int x, int y, const char* label, char icon);
  void drawAudioIcon(int cx, int cy, uint16_t bg);
  void drawLightIcon(int cx, int cy);
  void drawWifiIcon(int cx, int cy);
  void drawGameTile(int x, int y, const char* label, char icon, uint16_t iconColor);
  void drawQr(const char* text, int topY, int cx = ui::CENTER_X, int quiet = 3);
  void drawPillButton(int x, int y, int w, int h, const char* label, uint16_t bg);
  void drawFerret(FerretActor& ferret);
  void drawStatBar(int x, int y, const char* label, float value);
  void drawButtons();
  void drawIcon(ui::ButtonId id, int cx, int cy);
  void drawBattery(Battery& battery);
};
