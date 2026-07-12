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

  // Debug: stream the current canvas over Serial as raw RGB565, framed for the
  // host tool (tools/hwshot.py) to reassemble into a PNG.
  void captureScreenshot();

  // Web screenshot: the HTTP task (core 0) requests a snapshot, the render loop
  // (core 1) copies the finished canvas into a stable buffer, and the handler
  // then serves it. Avoids reading the canvas while it's being drawn.
  void requestWebSnapshot() { _snapReady = false; _snapReq = true; }
  bool webShotRequested() const { return _snapReq; }
  void takeWebSnapshot();  // main thread only
  bool webSnapshotReady() const { return _snapReady; }
  const uint16_t* webSnapshot() const { return _snap; }
  void clearWebSnapshot() { _snapReady = false; }

 private:
  LGFX_BallV2& _lcd;
  LGFX_Sprite  _canvas;

  int _pressedButton = -1;
  unsigned long _pressedUntil = 0;
  int _scrBright = 70;  // screen backlight brightness (0..100)

  uint16_t* _snap = nullptr;       // stable copy of the canvas for /shot.bmp
  volatile bool _snapReq = false;  // HTTP task -> render loop
  volatile bool _snapReady = false;

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
  void drawLeftHandle();  // "back" tab (config, games menu, Bolinha)
  void drawClock(Clock& clock);
  void drawDoodleFerret(int cx, int cy, bool faceLeft);
  // mode: 0 = idle, 1 = walking, 2 = jumping (celebration). zoom scales the
  // 40px game sprite (2 -> 80px, matching the scene ferret in Bolinha).
  void drawGameFerret(int cx, int cy, int mode, bool faceLeft, int zoom = 1);
  void drawTennisBall(int cx, int cy, int r);
  void drawMenu(ui::MenuPage page, int volume, int ledBright, int batteryPct,
                bool wifiOn, const char* ip);
  void drawMenuMain(int batteryPct, bool wifiOn, const char* ip);
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
  // A battery icon + % centered horizontally, top at `topY`.
  void drawBatteryPill(int topY, int pct, uint16_t outline, uint16_t txt);
  void drawStatBar(int x, int y, const char* label, float value);
  void drawButtons();
  void drawIcon(ui::ButtonId id, int cx, int cy);
};
