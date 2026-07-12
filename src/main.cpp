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
#include "SimonGame.h"
#include "DebugConsole.h"

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
SimonGame        simon;
DebugConsole     console;

// Which screen is showing.
enum Screen { SCREEN_PET, SCREEN_GAMES, SCREEN_DOODLE, SCREEN_BALL, SCREEN_SIMON };
static Screen screen = SCREEN_PET;

static unsigned long lastTickMs = 0;
static unsigned long lastSaveMs = 0;
static unsigned long lastInteractionMs = 0;  // for clock mode (idle timer)
static bool wasSleeping = false;
static bool menuOpen = false;
static ui::MenuPage menuPage = ui::PAGE_MAIN;  // which config sub-page is showing

// Shared raw-touch state for the game screens (press tracking, last position).
static bool g_touchDown = false;
static int32_t g_touchX = 0, g_touchY = 0;

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
    case ui::UI_MENU_BACK:  // left-edge "back": sub-page -> main, main -> close
      if (menuPage != ui::PAGE_MAIN) menuPage = ui::PAGE_MAIN;
      else menuOpen = false;
      break;
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

static bool g_shotPending = false;  // serial "shot": capture after the next render
// Serve pending screenshots (serial console + web portal) right after a render,
// when the canvas holds a complete frame. Both play the camera shutter.
static void serviceShots() {
  if (g_shotPending) {
    renderer.captureScreenshot();  // stream to serial (tools/hwshot.py)
    g_shotPending = false;
    audio.playCamera();
  }
  if (renderer.webShotRequested()) {
    renderer.takeWebSnapshot();     // copy for the HTTP /shot.bmp handler
    audio.playCamera();
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
      if (fresh && now - g_deadAtMs > DEATH_GRACE_MS) { audio.playClick(); leaveDoodle(); }
    } else if (ui::inGameBack(g_touchX, g_touchY)) {
      audio.playClick();
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
      audio.playClick();
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

// ---- Genius (Simon) screen ----
// The RGB LED mirrors the lit color (it "plays" the sequence with the arcs)
// and each color has its own tone. Presses resolve on touch-DOWN for snap.
static const uint8_t SIMON_RGB[4][3] = {
    {0, 255, 40},    // green
    {255, 0, 0},     // red
    {255, 170, 0},   // yellow
    {0, 70, 255},    // blue
};
static bool g_simonWasOver = false;
static unsigned long g_simonDeadAt = 0;
static int g_simonLastLit = -1;

static void startSimon(unsigned long now) {
  simon.reset();
  g_simonWasOver = false;
  g_simonDeadAt = 0;
  g_simonLastLit = -1;
  g_touchDown = false;
  led.endGame();
  led.gameOff();  // the LED belongs to the game now (dark between colors)
  screen = SCREEN_SIMON;
}

static void leaveSimon(unsigned long now) {
  led.endGame();  // LED back to the mood
  lastInteractionMs = now;
  screen = SCREEN_GAMES;
}

static void loopSimon(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);
  if (down && !g_touchDown) {
    g_touchDown = true;
    g_pressStartMs = now;
    g_touchX = x; g_touchY = y;
    if (!simon.gameOver()) {
      const int c = ui::simonColorAt(x, y);
      if (c >= 0) simon.press(c, now);
    }
  } else if (down) {
    g_touchX = x; g_touchY = y;
  } else if (g_touchDown) {  // release: exit button / dismiss game over
    g_touchDown = false;
    if (simon.gameOver()) {
      const bool fresh = g_pressStartMs > g_simonDeadAt;
      if (fresh && now - g_simonDeadAt > DEATH_GRACE_MS) {
        audio.playClick();
        leaveSimon(now);
        return;
      }
    } else if (ui::inSimonExit(g_touchX, g_touchY)) {
      audio.playClick();
      leaveSimon(now);
      return;
    }
  }

  // Color press coming from the phone (portal Genius pad).
  int wc;
  if (web.consumeSimonPress(wc) && !simon.gameOver()) simon.press(wc, now);

  simon.update(now);

  // One observer drives tone + LED off lit transitions - covers both the
  // device playing the sequence AND the player's press feedback.
  const int lit = simon.litColor();
  if (lit != g_simonLastLit) {
    if (lit >= 0) {
      audio.playSimon(lit);
      led.gameColor(SIMON_RGB[lit][0], SIMON_RGB[lit][1], SIMON_RGB[lit][2]);
    } else {
      led.gameOff();
    }
    g_simonLastLit = lit;
  }

  if (!g_simonWasOver && simon.gameOver()) {
    // New record earns the jingle; a plain miss gets the wrong-answer buzzer.
    simon.newRecord() ? audio.playRecord() : audio.playBuzzer();
    led.startDeath();
    g_simonDeadAt = now;
    web.setGameScore(simon.score());
    web.pushState();
  }
  g_simonWasOver = simon.gameOver();

  // Auto-close the game-over screen after the configured idle timeout.
  if (simon.gameOver()) {
    if (down) lastInteractionMs = now;
    const int timeout = petClock.menuTimeoutSec();
    const unsigned long from = lastInteractionMs > g_simonDeadAt ? lastInteractionMs : g_simonDeadAt;
    if (timeout > 0 && idleSince(now, from) > (unsigned long)timeout * 1000) {
      leaveSimon(now);
      return;
    }
  }

  renderer.drawSimon(simon);
}

// Games menu screen: tap a tile to start a game; back to the pet scene by
// pulling (or tapping) the left-edge tab. Auto-closes after the configured
// idle timeout (0 = never).
static int32_t g_gamesStartX = 0, g_gamesStartY = 0;  // where the touch began
static void loopGamesMenu(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);
  if (down && !g_touchDown) { g_touchDown = true; g_gamesStartX = x; g_gamesStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;  // touching resets the idle auto-close
  } else if (g_touchDown) {   // release: pull from the left edge = back, else tap
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_gamesStartX;
    const int32_t dy = g_touchY - g_gamesStartY;
    if (dx > 45 && abs(dx) > abs(dy) && g_gamesStartX < 65) {
      audio.playClick();
      screen = SCREEN_PET;  // pulled the left tab
    } else if (ui::inLeftHandle(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      screen = SCREEN_PET;  // tapped the tab
    } else if (ui::inGameDoodle(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      startDoodle(now);
    } else if (ui::inGameBall(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      ball.reset(); g_touchDown = false; screen = SCREEN_BALL;
    } else if (ui::inGameSimon(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      startSimon(now);
    }
  }
  const int timeout = petClock.menuTimeoutSec();
  if (timeout > 0 && screen == SCREEN_GAMES &&
      idleSince(now, lastInteractionMs) > (unsigned long)timeout * 1000) {
    screen = SCREEN_PET;
  }
  renderer.drawGamesMenu();
}

// Navigation/action commands from the DebugConsole (screen state lives here;
// module-level commands like vol:/led:/stats: are handled inside the console).
static bool consoleNavigate(const String& c) {
  const unsigned long now = millis();
  if (c == "shot")   { g_shotPending = true; return true; }  // captured post-render
  if (c == "pet")    { menuOpen = false; screen = SCREEN_PET; return true; }
  if (c == "games")  { menuOpen = false; screen = SCREEN_GAMES; return true; }
  if (c == "doodle") { startDoodle(now); return true; }
  if (c == "ball")   { ball.reset(); g_touchDown = false; screen = SCREEN_BALL; return true; }
  if (c == "simon")  { startSimon(now); return true; }

  if (c.startsWith("menu")) {
    screen = SCREEN_PET; menuOpen = true;
    const String pg = c.substring(4);  // "" or ":audio"/":luz"/":qr"/":main"
    menuPage = pg == ":audio" ? ui::PAGE_AUDIO
             : pg == ":luz"   ? ui::PAGE_LIGHT
             : pg == ":qr"    ? ui::PAGE_QR
                              : ui::PAGE_MAIN;
    return true;
  }

  if (c == "feed")  { doAction(ACTION_FEED);  return true; }
  if (c == "pat")   { doAction(ACTION_PAT);   return true; }
  if (c == "clean") { doAction(ACTION_CLEAN); return true; }
  if (c == "sleep") { doAction(ACTION_TOGGLE_SLEEP); return true; }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[homecritters] boot");

  renderer.begin();
  led.begin();
  battery.begin();
  input.begin();
  pet.begin();
  ferret.begin();
  petClock.begin();
  doodle.begin();  // load the Jump! high score
  simon.begin();   // load the Genius high score
  audio.begin();  // I2C + ES8311 + I2S + audio task
  web.begin(&pet, &audio, &led, &ferret, &petClock, &renderer, doAction);  // WiFi + portal
  web.setBattery(battery.percent());  // seed the portal value
  console.begin(&pet, &battery, &audio, &led, &renderer, consoleNavigate,
                []() { lastInteractionMs = millis(); });

  wasSleeping = pet.sleeping();
  lastTickMs = lastSaveMs = lastInteractionMs = millis();
  Serial.printf("[homecritters] ready. battery ~%d%%\n", battery.percent());
}

void loop() {
  const unsigned long now = millis();

  console.poll();  // debug console (screenshots + navigation)

  // --- WiFi setup mode (captive portal active): own screen; back tab exits ---
  if (web.configuring()) {
    web.process();
    int32_t x, y;
    const bool down = lcd.getTouch(&x, &y);
    static int32_t wcStartX = 0, wcStartY = 0;
    if (down && !g_touchDown) { g_touchDown = true; wcStartX = x; wcStartY = y; g_touchX = x; g_touchY = y; }
    else if (down) { g_touchX = x; g_touchY = y; }
    else if (g_touchDown) {  // release: left-edge pull or tap on the tab = exit
      g_touchDown = false;
      const int32_t dx = g_touchX - wcStartX, dy = g_touchY - wcStartY;
      if ((dx > 45 && abs(dx) > abs(dy) && wcStartX < 65) || ui::inLeftHandle(wcStartX, wcStartY)) {
        audio.playClick();
        web.cancelConfig();
      }
    }
    renderer.drawWifiConfig(web.apName());
    serviceShots();  // screenshots must work on this screen too
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

  // Battery: sample every few seconds (ADC read) and share with the portal.
  static unsigned long lastBattMs = 0;
  if (now - lastBattMs > 5000) {
    lastBattMs = now;
    web.setBattery(battery.percent());
  }

  // Media playback state changed (stream started/ended) -> tell HA/portal.
  static bool lastStreaming = false;
  if (audio.streaming() != lastStreaming) {
    lastStreaming = audio.streaming();
    web.pushState();
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
                screen == SCREEN_SIMON  ? "simon"  :
                screen == SCREEN_GAMES  ? "games"  : "pet");
  switch (web.consumeGameNav()) {
    case WebPortal::NAV_START:
      if (screen != SCREEN_DOODLE) { startDoodle(now); web.pushState(); }
      break;
    case WebPortal::NAV_BALL:
      if (screen != SCREEN_BALL) { ball.reset(); g_touchDown = false; screen = SCREEN_BALL; web.pushState(); }
      break;
    case WebPortal::NAV_SIMON:
      if (screen != SCREEN_SIMON) { startSimon(now); web.pushState(); }
      break;
    case WebPortal::NAV_BACK:
      if (screen == SCREEN_DOODLE) { leaveDoodle(); web.pushState(); }
      else if (screen == SCREEN_BALL) { leaveBall(now); web.pushState(); }
      else if (screen == SCREEN_SIMON) { leaveSimon(now); web.pushState(); }
      break;
    default: break;
  }

  // --- game screens ---
  if (screen == SCREEN_DOODLE) {
    web.setGameScore(doodle.score());
    loopDoodle(now);
    serviceShots();
    delay(12);
    return;
  }
  if (screen == SCREEN_BALL) {
    web.setGameScore(ball.catches());
    loopBall(now);
    serviceShots();
    delay(20);  // the forest backdrop is heavier to redraw; 50fps is plenty
    return;
  }
  if (screen == SCREEN_SIMON) {
    web.setGameScore(simon.score());
    loopSimon(now);
    serviceShots();
    delay(15);
    return;
  }
  if (screen == SCREEN_GAMES) {
    loopGamesMenu(now);
    serviceShots();
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
      audio.playClick();
      if (!menuOpen) screen = SCREEN_GAMES;
    } else if (ev.ui != ui::UI_NONE) {
      audio.playClick();  // every menu button/handle clicks
      handleUi(ev.ui);
    } else {
      // pet actions keep their own SFX (eat/pat/drink) - no click on top
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
  serviceShots();

  delay(30);
}
