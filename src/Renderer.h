#pragma once
#include "LGFX_BallV2.h"
#include "UiLayout.h"
#include "Theme.h"
#include "Pet.h"
#include "Battery.h"
#include "FerretActor.h"
#include "Clock.h"
#include "DoodleGame.h"

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
            bool menuOpen, int volume, bool wifiOn, const char* ip,
            bool clockActive, Clock& clock);

  // Full-screen WiFi setup screen (captive portal active) with Exit button.
  void drawWifiConfig(const char* apName);

  // Game screens.
  void drawGamesMenu();
  void drawDoodle(DoodleGame& game);

  // Visual feedback: highlight the tapped button for a few ms.
  void flashButton(int idx);

 private:
  LGFX_BallV2& _lcd;
  LGFX_Sprite  _canvas;

  int _pressedButton = -1;
  unsigned long _pressedUntil = 0;

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
  void drawMenu(int volume, bool wifiOn, const char* ip);
  void drawQr(const char* text, int topY);
  void drawPillButton(int x, int y, int w, int h, const char* label, uint16_t bg);
  void drawFerret(FerretActor& ferret);
  void drawStatBar(int x, int y, const char* label, float value);
  void drawButtons();
  void drawIcon(ui::ButtonId id, int cx, int cy);
  void drawBattery(Battery& battery);
};
