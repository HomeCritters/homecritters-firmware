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
#include "SimonGame.h"
#include "HaPanel.h"
#include "Weather.h"

// ============================================================
// Renderer: owns the display and the canvas (double buffer) and
// draws the whole scene. Knows no game rules - it only paints
// whatever Pet/Battery/FerretActor report.
// ============================================================

class Renderer {
 public:
  explicit Renderer(LGFX_BallV2& lcd) : _lcd(lcd), _canvas(&lcd) {}

  void begin();
  // mediaFx: 0 none, 1 music (party notes), 2 TTS (Alexa-style voice ring) -
  // mirrors AudioPlayer::MediaKind.
  // voiceState: 0 none, 1 listening, 2 thinking, 3 speaking - assistant voice
  // feedback ring (driven by the push-to-talk state machine in main).
  void draw(const Pet& pet, Battery& battery, FerretActor& ferret,
            bool menuOpen, ui::MenuPage menuPage, int volume, int ledBright,
            bool wifiOn, const char* ip, bool clockActive, Clock& clock,
            uint8_t mediaFx = 0, uint8_t voiceState = 0, bool micMuted = false,
            bool micLive = false);

  // Screen (backlight) brightness 0..100, persisted to NVS. The Renderer owns
  // the display, so it owns this too. Floored so the screen never goes dark.
  void setScreenBrightness(int pct);
  int screenBrightness() const { return _scrBright; }
  // Full-sleep blank: backlight hard off / restore (never persisted).
  void setDisplayOff(bool off);
  // Active pairing PIN ("" = none): draws a full-screen overlay with the
  // 6 digits in boxes. Driven by main from WebPortal::pairingPin().
  void setPairingPin(const char* p) { strlcpy(_pairPin, p, sizeof(_pairPin)); }
  // Authed clients summary for Seguranca > Aparelhos (refreshed by main).
  void setClientsInfo(const char* s) { strlcpy(_clientsInfo, s, sizeof(_clientsInfo)); }
  // Revoke button state: false = "Revogar acesso", true = "Confirma?" (armed).
  void setRevokeArmed(bool a) { _revokeArmed = a; }

  // Full-screen WiFi setup screen (captive portal active) with Exit button.
  void drawWifiConfig(const char* apName);

  // Game screens.
  void drawGamesMenu();
  void drawDoodle(DoodleGame& game);
  void drawBall(BallGame& game);
  void drawSimon(SimonGame& game);

  // Home Assistant panel: paginated 2x2 grid of entity tiles. `loading` shows
  // a spinner while the first ha:list hasn't arrived yet (fresh subscribe).
  void drawHaPanel(HaPanel& ha, int page, bool loading = false);

  // Weather: the current WMO code tints/animates the pet scene (set each
  // frame by main; 0 = clear). temp (INT8_MIN = unknown) feeds the little
  // condition+temperature line under the idle clock.
  void setWeather(uint8_t wmoCode, int8_t temp = INT8_MIN) {
    _wxCode = wmoCode;
    _wxTemp = temp;
  }
  void drawWeather(Weather& wx);
  // Debug/console: fire a lightning strike right now (validates bolt+thunder).
  void triggerBolt() { _flashFrames = 6; _thunderPending = true; }
  // A storm flash queued a thunder clap - main consumes it and plays the SFX.
  bool consumeThunder() {
    const bool t = _thunderPending;
    _thunderPending = false;
    return t;
  }

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
  bool _displayOff = false;  // full-sleep blank (setDisplayOff)
  char _pairPin[7] = {0};       // active pairing PIN ("" = no overlay)
  char _clientsInfo[120] = {0}; // authed WS clients ('\n'-separated)
  bool _revokeArmed = false;    // revoke button in "Confirma?" state

  uint16_t* _snap = nullptr;       // stable copy of the canvas for /shot.bmp
  volatile bool _snapReq = false;  // HTTP task -> render loop
  volatile bool _snapReady = false;

  // Palette active for the current frame (day or night). Set at the top
  // of draw() and read by the helpers, instead of threading a parameter.
  theme::ScenePalette _p = theme::NIGHT;

  // Real-weather scene state (set by main from the Weather model).
  uint8_t _wxCode = 0;             // current WMO code (0 = clear)
  int8_t _wxTemp = INT8_MIN;       // current temp (INT8_MIN = unknown)
  unsigned long _nextFlashMs = 0;  // next lightning flash (storm)
  uint8_t _flashFrames = 0;        // frames left of the current flash
  int16_t _boltX = 120;            // where the current bolt strikes
  uint32_t _boltSeed = 1;          // jitter seed (stable during one strike)
  bool _thunderPending = false;    // flash fired -> main plays the thunder

  void drawSky();
  void drawStars();
  void drawMoon();
  void drawSun();
  void drawSunset();
  void drawForest(bool night);   // treeline + cabin + grass + trees
  void drawCabin(int bx, int by, bool night);
  void drawPineTree(int bx, int baseY, int size);
  void drawSparkles(bool night);
  void drawHeader(const Pet& pet, bool wifiOn, bool micMuted, bool micLive);
  void drawMenuHandle();
  void drawRightHandle();
  void drawLeftHandle();    // "back" tab (config, games menu, Bolinha)
  void drawBottomHandle();  // weather forecast tab (pet screen)
  // Weather scene FX + forecast-screen helpers.
  theme::ScenePalette tintPalette(const theme::ScenePalette& p, uint8_t t);
  void drawClouds(uint8_t n);
  void drawRain(uint8_t intensity, bool freezing);
  void drawSnow(uint8_t intensity, bool grains, bool fast);
  void drawHail();
  void drawLightning();
  void drawFog();
  // Weather glyph straight from the WMO code (granular: sun-behind-cloud,
  // fog dashes, snow flakes, hail pellets...). s = scale (1 mini, 2 big).
  void drawWxIcon(int cx, int cy, uint8_t code, int s);
  void drawClock(Clock& clock);
  void drawMediaFx(uint8_t kind);     // 1 = disco ball + lasers (music party)
  void drawVoiceRing(uint8_t state);  // 1 listening, 2 thinking, 3 speaking
  void drawDiscoFloor();           // checkered dance floor (under the pet)
  void drawDoodleFerret(int cx, int cy, bool faceLeft);
  // mode: 0 = idle, 1 = walking, 2 = jumping (celebration). zoom scales the
  // 40px game sprite (2 -> 80px, matching the scene ferret in Bolinha).
  void drawGameFerret(int cx, int cy, int mode, bool faceLeft, float zoom = 1.0f);
  void drawTennisBall(int cx, int cy, int r);
  void drawMenu(ui::MenuPage page, int volume, int ledBright, int batteryPct,
                bool wifiOn, const char* ip);
  void drawMenuMain(int batteryPct, bool wifiOn, const char* ip);
  void drawMenuAudio(int volume);
  void drawMenuLight(int ledBright);
  void drawPageHeader(const char* title, const char* sub);  // bezel-safe title
  void drawMenuConn(bool wifiOn, const char* ip);  // Conexao: WiFi | Portal
  void drawMenuQr(bool wifiOn, const char* ip);    // Portal (QR) detail
  void drawMenuSec();      // Seguranca: Dispositivos | Parear
  void drawMenuSecHa();    // paired clients list ("Dispositivos")
  void drawPairingOverlay();  // full-screen 6-digit PIN (auto-shown)
  void drawStepper(const char* label, int pct, const ui::ButtonSlot& minus,
                   const ui::ButtonSlot& plus);
  void drawGridCell(int x, int y, const char* label, char icon);
  void drawAudioIcon(int cx, int cy, uint16_t bg);
  void drawLightIcon(int cx, int cy, bool on = true);
  void drawWifiIcon(int cx, int cy);
  void drawLockIcon(int cx, int cy);   // Seguranca tile (padlock)
  void drawHouseIcon(int cx, int cy);  // HA tile
  void drawKeyIcon(int cx, int cy);    // Senha tile
  void drawQrGlyph(int cx, int cy);    // Portal tile (mini QR)
  // HA panel domain icons.
  void drawThermoIcon(int cx, int cy);
  void drawDropIcon(int cx, int cy);
  void drawSwitchIcon(int cx, int cy, bool on);
  void drawFanIcon(int cx, int cy);
  void drawPersonIcon(int cx, int cy, bool present);  // presence/motion
  void drawSunSmallIcon(int cx, int cy);              // illuminance
  void drawHaTile(int x, int y, const HaPanel::Entity& e);
  // Draw text inside [x, x+w]: centered if it fits, else clipped to the box and
  // scrolled horizontally (marquee) so it never overflows a button/tile.
  void drawScrollText(int x, int y, int w, const char* s, uint16_t color,
                      uint8_t size);
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
