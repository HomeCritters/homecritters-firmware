#include <Arduino.h>
#include "LGFX_BallV2.h"
#include "GameConfig.h"
#include "Pet.h"
#include "Battery.h"
#include "StatusLed.h"
#include "Renderer.h"
#include "InputController.h"
#include "AudioPlayer.h"
#include "FerretActor.h"
#include "WebPortal.h"
#include "Clock.h"
#include "DoodleGame.h"

// ============================================================
// Desk tamagotchi (ferret) - Ball V2
// main.cpp is orchestration only: it wires the modules together
// and runs the loop. The real logic lives in each module.
// ============================================================

LGFX_BallV2     lcd;
Pet             pet;
Battery         battery;
StatusLed       led;
Renderer        renderer(lcd);
InputController  input;
AudioPlayer      audio;
FerretActor      ferret;
WebPortal        web;
Clock            petClock;
DoodleGame       doodle;

// Which screen is showing.
enum Screen { SCREEN_PET, SCREEN_GAMES, SCREEN_DOODLE };
static Screen screen = SCREEN_PET;

static unsigned long lastTickMs = 0;
static unsigned long lastSaveMs = 0;
static unsigned long lastInteractionMs = 0;  // for clock mode (idle timer)
static bool wasSleeping = false;
static bool menuOpen = false;

// Release-based tap for the game screens: true once when a finger lifts.
static bool g_touchDown = false;
static int32_t g_touchX = 0, g_touchY = 0;
static bool tapReleased(int32_t& ox, int32_t& oy) {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) { g_touchDown = true; g_touchX = x; g_touchY = y; return false; }
  if (g_touchDown) { g_touchDown = false; ox = g_touchX; oy = g_touchY; return true; }
  return false;
}

// Runs a game action (shared by touch, BOOT button and the web portal).
// The sleep/wake sound is played by the transition detector in loop().
void doAction(Action a) {
  lastInteractionMs = millis();  // any action (web included) leaves clock mode
  pet.apply(a);
  switch (a) {
    case ACTION_FEED:  ferret.onFeed(); audio.playEat();   break;
    case ACTION_PAT:   ferret.onPat();  audio.playPat();   break;
    case ACTION_CLEAN: audio.playDrink();                  break;
    default: break;
  }
}

static void handleUi(ui::UiHit hit) {
  switch (hit) {
    case ui::UI_MENU_TOGGLE: menuOpen = !menuOpen; break;
    case ui::UI_VOL_DOWN:    audio.setVolume(audio.volume() - 10); break;
    case ui::UI_VOL_UP:      audio.setVolume(audio.volume() + 10); break;
    case ui::UI_WIFI:        menuOpen = false; web.startConfigPortal(); break;
    default: break;
  }
}

static bool g_doodleWasDead = false;
static unsigned long g_deadAtMs = 0;      // when the current game over started
static unsigned long g_pressStartMs = 0;  // when the current touch began

// After a game over, ignore taps for this long AND require a fresh press that
// started after the death - so the same finger that killed you (or a reflex
// tap) can't dismiss the score screen by accident.
static constexpr unsigned long DEATH_GRACE_MS = 900;

static void startDoodle(unsigned long now) {
  doodle.reset();
  g_doodleWasDead = false;
  g_deadAtMs = 0;
  g_touchDown = false;
  led.endGame();  // clear any leftover death effect
  screen = SCREEN_DOODLE;
}

// Leave Doodle Jump back to the games menu (also stops the game-over LED).
// Refresh the idle timer so the menu doesn't instantly auto-close on return.
static void leaveDoodle() {
  led.endGame();
  lastInteractionMs = millis();
  screen = SCREEN_GAMES;
}

// Doodle Jump screen: the ferret follows the finger (hardware touch) or the
// phone joystick (over WebSocket). Back to the menu on the corner button or a
// fresh tap after game over.
static void loopDoodle(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);

  // Horizontal target: hardware touch wins; otherwise follow the phone.
  float targetX = -1.0f;
  if (down && !ui::inGameBack(x, y)) {
    targetX = (float)x;
  } else {
    const float wnorm = web.gameTargetXNorm();
    if (wnorm >= 0) targetX = wnorm * 240.0f;
  }
  doodle.update(now, targetX);

  if (doodle.bounced()) doodle.boosted() ? audio.playBoost() : audio.playJump();
  if (!g_doodleWasDead && doodle.gameOver()) {
    audio.playDeath();
    led.startDeath();  // 3 fast red blinks, then solid red until leaving
    g_deadAtMs = now;
    web.setGameScore(doodle.score());
    web.pushState();  // let the phone show the final score
  }
  g_doodleWasDead = doodle.gameOver();

  // Touch: track press start so we can require a "fresh" tap to dismiss.
  if (down && !g_touchDown) { g_touchDown = true; g_pressStartMs = now; g_touchX = x; g_touchY = y; }
  else if (down) { g_touchX = x; g_touchY = y; }
  else if (g_touchDown) {  // release
    g_touchDown = false;
    if (doodle.gameOver()) {
      const bool fresh = g_pressStartMs > g_deadAtMs;  // began after the death
      if (fresh && now - g_deadAtMs > DEATH_GRACE_MS) leaveDoodle();
    } else if (ui::inGameBack(g_touchX, g_touchY)) {
      leaveDoodle();
    }
  }

  // Auto-close the game-over screen after the configured idle timeout (0 =
  // never). The countdown restarts on any touch of the death screen.
  if (screen == SCREEN_DOODLE && doodle.gameOver()) {
    if (down) lastInteractionMs = now;
    const int timeout = petClock.menuTimeoutSec();
    const unsigned long idleFrom = lastInteractionMs > g_deadAtMs ? lastInteractionMs : g_deadAtMs;
    if (timeout > 0 && now - idleFrom > (unsigned long)timeout * 1000) leaveDoodle();
  }
  renderer.drawDoodle(doodle);
}

// Games menu screen: tap a game to start, or Back to the pet scene. Auto-closes
// to the pet scene after the configured idle timeout (0 = never).
static void loopGamesMenu(unsigned long now) {
  int32_t tx, ty;
  const bool tapped = tapReleased(tx, ty);
  if (tapped) {
    if (ui::inGameDoodle(tx, ty)) startDoodle(now);
    else if (ui::inGamesBack(tx, ty)) screen = SCREEN_PET;
  }
  if (g_touchDown || tapped) lastInteractionMs = now;  // any touch resets idle
  const int timeout = petClock.menuTimeoutSec();
  if (timeout > 0 && screen == SCREEN_GAMES &&
      now - lastInteractionMs > (unsigned long)timeout * 1000) {
    screen = SCREEN_PET;
  }
  renderer.drawGamesMenu();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[tamagotchi] boot - ferret Ball V2");

  renderer.begin();
  led.begin();
  battery.begin();
  input.begin();
  pet.begin();
  ferret.begin();
  petClock.begin();
  audio.begin();  // I2C + ES8311 + I2S + audio task
  web.begin(&pet, &audio, &led, &ferret, &petClock, doAction);  // WiFi + portal

  wasSleeping = pet.sleeping();
  lastTickMs = lastSaveMs = lastInteractionMs = millis();
  Serial.printf("[tamagotchi] ready. battery ~%d%%\n", battery.percent());
}

void loop() {
  const unsigned long now = millis();

  // --- WiFi setup mode (captive portal active): own screen + Exit ---
  if (web.configuring()) {
    web.process();
    int32_t tx, ty;
    if (lcd.getTouch(&tx, &ty) && ui::inWifiExit(tx, ty)) web.cancelConfig();
    renderer.drawWifiConfig(web.apName());
    delay(50);
    return;
  }

  // --- shared background (runs on every screen) ---
  const float deltaMin = (now - lastTickMs) / 60000.0f;
  if (deltaMin > 0) {
    pet.update(deltaMin);
    lastTickMs = now;
  }
  petClock.update(web.connected());

  const bool sleeping = pet.sleeping();
  if (sleeping && !wasSleeping) {
    audio.playSleepTune();
  } else if (!sleeping && wasSleeping) {
    audio.playWake();
  }
  wasSleeping = sleeping;

  if (now - lastSaveMs > game::SAVE_INTERVAL_MS) {
    pet.save();
    lastSaveMs = now;
  }

  web.handle();

  ferret.update(pet, now);
  static uint32_t lastSeq = 0xFFFFFFFF;
  static bool lastFlip = false;
  if (ferret.animSeq() != lastSeq || ferret.faceLeft() != lastFlip) {
    lastSeq = ferret.animSeq();
    lastFlip = ferret.faceLeft();
    web.pushState();
  }

  led.update(pet.mood());

  // Report the current screen to the portal and honor phone game nav (start/
  // back), so Doodle Jump can be launched and steered entirely from the phone.
  web.setScreen(screen == SCREEN_DOODLE ? "doodle" : screen == SCREEN_GAMES ? "games" : "pet");
  switch (web.consumeGameNav()) {
    case WebPortal::NAV_START:
      if (screen != SCREEN_DOODLE) { startDoodle(now); web.pushState(); }
      break;
    case WebPortal::NAV_BACK:
      if (screen == SCREEN_DOODLE) { leaveDoodle(); web.pushState(); }
      break;
    default: break;
  }

  // --- game screens ---
  if (screen == SCREEN_DOODLE) {
    web.setGameScore(doodle.score());
    loopDoodle(now);
    delay(12);
    return;
  }
  if (screen == SCREEN_GAMES) {
    loopGamesMenu(now);
    delay(30);
    return;
  }

  // --- pet screen ---
  const bool clockActive = petClock.enabled() && petClock.synced() && !menuOpen &&
                           (now - lastInteractionMs > (unsigned long)petClock.idleSec() * 1000);

  InputEvent ev = input.poll(lcd, menuOpen);
  if (ev.ui != ui::UI_NONE || ev.action != ACTION_NONE) {
    lastInteractionMs = now;
    if (clockActive) {
      // in clock mode a touch only wakes the screen (doesn't run the action)
    } else if (ev.ui == ui::UI_GAMES_TOGGLE) {
      if (!menuOpen) screen = SCREEN_GAMES;
    } else if (ev.ui != ui::UI_NONE) {
      handleUi(ev.ui);
    } else {
      doAction(ev.action);
      if (ev.buttonIdx >= 0) renderer.flashButton(ev.buttonIdx);
    }
  }

  // Auto-close the config menu after the configured idle timeout (0 = never).
  const int menuTimeout = petClock.menuTimeoutSec();
  if (menuOpen && menuTimeout > 0 && now - lastInteractionMs > (unsigned long)menuTimeout * 1000) {
    menuOpen = false;
  }

  String ip = web.connected() ? web.ip() : String();
  renderer.draw(pet, battery, ferret, menuOpen, audio.volume(), web.connected(),
                ip.c_str(), clockActive, petClock);

  delay(30);
}
