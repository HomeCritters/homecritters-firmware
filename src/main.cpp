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
#include "BallGame.h"

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
BallGame         ball;

// Which screen is showing.
enum Screen { SCREEN_PET, SCREEN_GAMES, SCREEN_DOODLE, SCREEN_BALL };
static Screen screen = SCREEN_PET;

static unsigned long lastTickMs = 0;
static unsigned long lastSaveMs = 0;
static unsigned long lastInteractionMs = 0;  // for clock mode (idle timer)
static bool wasSleeping = false;
static bool menuOpen = false;
static ui::MenuPage menuPage = ui::PAGE_MAIN;  // which config sub-page is showing

// Release-based tap for the game screens: true once when a finger lifts.
static bool g_touchDown = false;
static int32_t g_touchX = 0, g_touchY = 0;
static bool tapReleased(int32_t& ox, int32_t& oy) {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) { g_touchDown = true; g_touchX = x; g_touchY = y; return false; }
  if (g_touchDown) { g_touchDown = false; ox = g_touchX; oy = g_touchY; return true; }
  return false;
}

// Idle time since a timestamp, saturating at 0. A web command handled in
// web.handle() sets lastInteractionMs to a millis() slightly AFTER this loop's
// captured `now`; a plain `now - since` would underflow (unsigned) to a huge
// value and, e.g., flash the idle clock for a frame. This clamps that away.
static unsigned long idleSince(unsigned long now, unsigned long since) {
  return now >= since ? now - since : 0;
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
    case ui::UI_MENU_TOGGLE:
      menuOpen = !menuOpen;
      if (menuOpen) menuPage = ui::PAGE_MAIN;  // always open on the main page
      break;
    case ui::UI_MENU_BACK:   menuPage = ui::PAGE_MAIN;  break;
    case ui::UI_OPEN_AUDIO:  menuPage = ui::PAGE_AUDIO; break;
    case ui::UI_OPEN_LIGHT:  menuPage = ui::PAGE_LIGHT; break;
    case ui::UI_OPEN_QR:     menuPage = ui::PAGE_QR;    break;
    case ui::UI_VOL_DOWN:    audio.setVolume(audio.volume() - 10); break;
    case ui::UI_VOL_UP:      audio.setVolume(audio.volume() + 10); break;
    case ui::UI_LED_DOWN:    led.setBrightness(led.brightness() - 10); break;
    case ui::UI_LED_UP:      led.setBrightness(led.brightness() + 10); break;
    case ui::UI_SCR_DOWN:    renderer.setScreenBrightness(renderer.screenBrightness() - 10); break;
    case ui::UI_SCR_UP:      renderer.setScreenBrightness(renderer.screenBrightness() + 10); break;
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

  if (doodle.bounced()) {
    if (doodle.boosted())       audio.playBoost();
    else if (doodle.crumbled()) audio.playCrumble();  // dirt platform broke
    else                        audio.playJump();
  }
  if (!g_doodleWasDead && doodle.gameOver()) {
    // A new record earns the celebration jingle instead of the death sound.
    doodle.newRecord() ? audio.playRecord() : audio.playDeath();
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
    if (timeout > 0 && idleSince(now, idleFrom) > (unsigned long)timeout * 1000) leaveDoodle();
  }
  renderer.drawDoodle(doodle);
}

// Bolinha (fetch) screen: swipe up to throw (hardware touch or the phone);
// the ferret chases and catches. Exit via the back button or the phone.
static int32_t g_ballDragX = 0, g_ballDragY = 0;  // where the current touch began
static void leaveBall(unsigned long now) {
  lastInteractionMs = now;
  screen = SCREEN_GAMES;
}

static void loopBall(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);
  if (down && !g_touchDown) {
    g_touchDown = true;
    g_ballDragX = x; g_ballDragY = y;
  }
  if (down) { g_touchX = x; g_touchY = y; }
  else if (g_touchDown) {  // release: left-edge pull to exit, or a throw
    g_touchDown = false;
    const int32_t dx = g_touchX - g_ballDragX;
    const int32_t dy = g_touchY - g_ballDragY;
    if (g_ballDragX < 26 && dx > 45 && abs(dx) > abs(dy)) {
      leaveBall(now);  // pull from the left edge -> quit the game
    } else if (ball.ready() && dy < -25 && abs(dy) > abs(dx)) {
      // upward swipe = throw (direction/force follow the gesture)
      float vx = constrain((float)dx * 3.0f, -420.0f, 420.0f);
      float vy = constrain((float)dy * 3.2f, -760.0f, -360.0f);
      ball.throwBall(vx, vy);
      audio.playThrow();
    }
  }

  // Throw requested from the phone (normalized swipe -> same speed ranges).
  float nx, ny;
  if (web.consumeBallThrow(nx, ny) && ball.ready() && ny < -0.08f) {
    float vx = constrain(nx * 900.0f, -420.0f, 420.0f);
    float vy = constrain(ny * 1000.0f, -760.0f, -360.0f);
    ball.throwBall(vx, vy);
    audio.playThrow();
  }

  ball.update(now);
  if (ball.takeCaught()) audio.playPat();  // caught it!
  renderer.drawBall(ball);
}

// Games menu screen: tap a game to start, or Back to the pet scene. Auto-closes
// to the pet scene after the configured idle timeout (0 = never).
static void loopGamesMenu(unsigned long now) {
  int32_t tx, ty;
  const bool tapped = tapReleased(tx, ty);
  if (tapped) {
    if (ui::inGameDoodle(tx, ty)) startDoodle(now);
    else if (ui::inGameBall(tx, ty)) { ball.reset(); g_touchDown = false; screen = SCREEN_BALL; }
    else if (ui::inGamesBack(tx, ty)) screen = SCREEN_PET;
  }
  if (g_touchDown || tapped) lastInteractionMs = now;  // any touch resets idle
  const int timeout = petClock.menuTimeoutSec();
  if (timeout > 0 && screen == SCREEN_GAMES &&
      idleSince(now, lastInteractionMs) > (unsigned long)timeout * 1000) {
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
  doodle.begin();  // load the Jump! high score
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
  // Mirror the pet's animation to the portal only on the pet screen; during a
  // game the portal isn't showing the pet, so skip the extra broadcasts.
  static uint32_t lastSeq = 0xFFFFFFFF;
  static bool lastFlip = false;
  if (ferret.animSeq() != lastSeq || ferret.faceLeft() != lastFlip) {
    lastSeq = ferret.animSeq();
    lastFlip = ferret.faceLeft();
    if (screen == SCREEN_PET) web.pushState();
  }

  led.update(pet.mood());

  // Report the current screen to the portal and honor phone game nav (start/
  // back), so Doodle Jump can be launched and steered entirely from the phone.
  web.setScreen(screen == SCREEN_DOODLE ? "doodle" :
                screen == SCREEN_BALL   ? "ball"   :
                screen == SCREEN_GAMES  ? "games"  : "pet");
  switch (web.consumeGameNav()) {
    case WebPortal::NAV_START:
      if (screen != SCREEN_DOODLE) { startDoodle(now); web.pushState(); }
      break;
    case WebPortal::NAV_BALL:
      if (screen != SCREEN_BALL) { ball.reset(); g_touchDown = false; screen = SCREEN_BALL; web.pushState(); }
      break;
    case WebPortal::NAV_BACK:
      if (screen == SCREEN_DOODLE) { leaveDoodle(); web.pushState(); }
      else if (screen == SCREEN_BALL) { leaveBall(now); web.pushState(); }
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
  if (screen == SCREEN_BALL) {
    web.setGameScore(ball.catches());
    loopBall(now);
    delay(20);  // the forest backdrop is heavier to redraw; 50fps is plenty
    return;
  }
  if (screen == SCREEN_GAMES) {
    loopGamesMenu(now);
    delay(30);
    return;
  }

  // --- pet screen ---
  const bool clockActive = petClock.enabled() && petClock.synced() && !menuOpen &&
                           (idleSince(now, lastInteractionMs) > (unsigned long)petClock.idleSec() * 1000);

  InputEvent ev = input.poll(lcd, menuOpen, menuPage);
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
  if (menuOpen && menuTimeout > 0 &&
      idleSince(now, lastInteractionMs) > (unsigned long)menuTimeout * 1000) {
    menuOpen = false;
  }

  // The IP string is only rendered inside the config menu; skip the per-frame
  // String allocation otherwise.
  String ip;
  if (menuOpen && web.connected()) ip = web.ip();
  renderer.draw(pet, battery, ferret, menuOpen, menuPage, audio.volume(),
                led.brightness(), web.connected(), ip.c_str(), clockActive, petClock);

  delay(30);
}
