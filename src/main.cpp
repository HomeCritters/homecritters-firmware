#include <Arduino.h>
#include "LGFX_BallV2.h"
#include "GameConfig.h"
#include "Pet.h"
#include "Battery.h"
#include "StatusLed.h"
#include "Renderer.h"
#include "InputController.h"
#include "TouchInput.h"
#include "AudioPlayer.h"
#include "audio/AudioCodec.h"  // mixer debug (overlayActive)
#include "FerretActor.h"
#include "WebPortal.h"
#include "Clock.h"
#include "DoodleGame.h"
#include "BallGame.h"
#include "SimonGame.h"
#include "HaPanel.h"
#include "Walkie.h"
#include "Weather.h"
#include "DebugConsole.h"
#include "pins.h"  // BOOT pin (full-sleep wake check)
#include <Preferences.h>  // night-mode sound settings
#include <esp_task_wdt.h>  // task watchdog (hang -> reboot)
#include <WiFi.h>          // diag command (IP/RSSI)
#include "TaskRegistry.h"  // `top` roster (render loop registers itself)

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
HaPanel          haPanel;
Walkie           walkie;
Weather          weather;
DebugConsole     console;

// Which screen is showing.
enum Screen { SCREEN_PET, SCREEN_GAMES, SCREEN_DOODLE, SCREEN_BALL, SCREEN_SIMON, SCREEN_HA, SCREEN_WEATHER, SCREEN_WALKIE, SCREEN_WALKIE_TALK };
static Screen screen = SCREEN_PET;
// Single entry point for switching screens (defined after the loopX handlers).
static void enterScreen(Screen s, unsigned long now);
static int g_haPage = 0;  // HA panel current page

static unsigned long lastTickMs = 0;
static unsigned long lastSaveMs = 0;
static unsigned long lastInteractionMs = 0;  // for clock mode (idle timer)
static bool wasSleeping = false;
static bool menuOpen = false;
static ui::MenuPage menuPage = ui::PAGE_MAIN;  // which config sub-page is showing

// Shared raw-touch state for the game screens (press tracking, last position).
static bool g_touchDown = false;
static int32_t g_touchX = 0, g_touchY = 0;

// Short label for the boot log ("why did I reboot?"). A 24/7 device needs the
// black box: without this, a watchdog/panic reset is indistinguishable from a
// power cycle.
static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT-WDT";
    case ESP_RST_TASK_WDT: return "TASK-WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:return "deepsleep";
    default:               return "unknown";
  }
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

static unsigned long g_revokeArmedUntil = 0;  // revoke 2-tap confirm window

static void handleUi(ui::UiHit hit) {
  switch (hit) {
    case ui::UI_MENU_TOGGLE:
      menuOpen = !menuOpen;
      if (menuOpen) menuPage = ui::PAGE_MAIN;  // always open on the main page
      break;
    case ui::UI_MENU_BACK:  // left-edge "back": one level up, main -> close
      if (menuPage != ui::PAGE_MAIN) menuPage = ui::menuParent(menuPage);
      else menuOpen = false;
      break;
    case ui::UI_OPEN_AUDIO:  menuPage = ui::PAGE_AUDIO;  break;
    case ui::UI_OPEN_LIGHT:  menuPage = ui::PAGE_LIGHT;  break;
    case ui::UI_OPEN_CONN:   menuPage = ui::PAGE_CONN;   break;
    case ui::UI_OPEN_QR:     menuPage = ui::PAGE_QR;     break;
    case ui::UI_OPEN_SEC:    menuPage = ui::PAGE_SEC;    break;
    case ui::UI_OPEN_HA_INFO: menuPage = ui::PAGE_SEC_HA; break;
    case ui::UI_PAIR:        web.startPairing(); menuOpen = false; break;
    case ui::UI_REVOKE:  // two-tap confirm: arm first, revoke on the second
      if (millis() < g_revokeArmedUntil) {
        g_revokeArmedUntil = 0;
        web.revokeAll();
        audio.playBuzzer();  // destructive-action feedback
      } else {
        g_revokeArmedUntil = millis() + 4000;
      }
      break;
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

static int g_festDebug = -1;  // serial "fest:X": -1 auto, else forced Fest (0-3), 9=bday
// The pet's birthday, NVS "pet"/"bday" as "YYYY-MM-DD" (legacy "MM-DD" still
// loads). Default = the project's first commit (2026-07-09). The party fires
// on the MM-DD part; the year is only for the portal's age display. Balloons +
// confetti all day + "parabens pra voce" once.
static char g_bdayDate[11] = "2026-07-09";
// The MM-DD slice of g_bdayDate (handles both the full ISO date and legacy).
static const char* bdayMonthDay() {
  const size_t n = strlen(g_bdayDate);
  return n >= 10 ? g_bdayDate + 5 : g_bdayDate;  // "YYYY-MM-DD"+5 -> "MM-DD"
}
static bool g_shotPending = false;  // serial "shot": capture after the next render
static int g_voiceDebug = -1;       // serial "voice:N": force a voice ring (-1 = off)
static int g_wxDebug = -1;          // serial "wxset:X": force a scene weather (-1 = off)
static bool g_fullSleep = false;    // night mode: screen+LED dark, pet asleep
static unsigned long g_inputSwallowUntil = 0;  // discard input right after wake
// Night-mode sound settings (NVS): play the snore/wake tune on FULL-sleep
// transitions? The regular sleep button always keeps its sounds.
static bool g_nightSleepSnd = true, g_nightWakeSnd = true;
static bool g_muteNextSleepSnd = false, g_muteNextWakeSnd = false;
// Was the mic already muted when night mode engaged? (wake restores this)
static bool g_micMutedBeforeNight = false;
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
  web.pumpScreen(renderer);  // live screen stream to the portal (no-op if idle)
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
  const bool down = touchinput::read(lcd, &x, &y);

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
  const bool down = touchinput::read(lcd, &x, &y);
  if (down && !g_touchDown) {
    g_touchDown = true;
    g_ballDragX = x; g_ballDragY = y;
  }
  if (down) { g_touchX = x; g_touchY = y; }
  else if (g_touchDown) {  // release: left-edge pull to exit, or a throw
    g_touchDown = false;
    const int32_t dx = g_touchX - g_ballDragX;
    const int32_t dy = g_touchY - g_ballDragY;
    if (g_ballDragX < 26 && dx > ui::SWIPE_MIN && abs(dx) > abs(dy)) {
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
  const bool down = touchinput::read(lcd, &x, &y);
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
  const bool down = touchinput::read(lcd, &x, &y);
  if (down && !g_touchDown) { g_touchDown = true; g_gamesStartX = x; g_gamesStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;  // touching resets the idle auto-close
  } else if (g_touchDown) {   // release: pull from the left edge = back, else tap
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_gamesStartX;
    const int32_t dy = g_touchY - g_gamesStartY;
    if (ui::isBackPull(dx, dy, g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      screen = SCREEN_PET;  // pulled or tapped the left tab
    } else if (ui::inGameDoodle(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      enterScreen(SCREEN_DOODLE, now);
    } else if (ui::inGameBall(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      enterScreen(SCREEN_BALL, now);
    } else if (ui::inGameSimon(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      enterScreen(SCREEN_SIMON, now);
    } else if (ui::inGameWalkie(g_gamesStartX, g_gamesStartY)) {
      audio.playClick();
      enterScreen(SCREEN_WALKIE, now);
    }
  }
  renderer.drawGamesMenu();  // idle auto-close lives in the screen table
}

// ---- Walkie-talkie screens ----
// List: toggle + "Todos" + discovered peers; tap a row -> talk screen.
static int32_t g_wtStartX = 0, g_wtStartY = 0;
static int g_wtTarget = -1;        // -1 = broadcast, else peer index
static char g_wtTargetName[19] = "Todos";
static void loopWalkieList(unsigned long now) {
  int32_t x, y;
  const bool down = touchinput::read(lcd, &x, &y);
  if (down && !g_touchDown) { g_touchDown = true; g_wtStartX = x; g_wtStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;
  } else if (g_touchDown) {
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_wtStartX, dy = g_touchY - g_wtStartY;
    if (ui::isBackPull(dx, dy, g_wtStartX, g_wtStartY)) {
      audio.playClick();
      screen = SCREEN_GAMES;
    } else if (ui::inWalkiePill(g_wtStartX, g_wtStartY)) {
      audio.playClick();
      walkie.setEnabled(!walkie.enabled());
    } else if (walkie.enabled()) {
      const int row = ui::walkieRowAt(g_wtStartX, g_wtStartY);
      if (row == 0) {
        audio.playClick();
        g_wtTarget = -1;
        strlcpy(g_wtTargetName, "Todos", sizeof(g_wtTargetName));
        enterScreen(SCREEN_WALKIE_TALK, now);
      } else if (row > 0 && row - 1 < walkie.peerCount()) {
        audio.playClick();
        g_wtTarget = row - 1;
        strlcpy(g_wtTargetName, walkie.peer(row - 1).name, sizeof(g_wtTargetName));
        enterScreen(SCREEN_WALKIE_TALK, now);
      }
    }
  }
  // Refresh discovery every 10s while the list is open.
  static unsigned long nextScan = 0;
  if (now >= nextScan) { nextScan = now + 10000; walkie.requestScan(); }
  renderer.drawWalkieList(walkie);
}

// Talk: HOLD the big circle = transmit (down/up edges, not release-tap).
static bool g_wtHeld = false;
static void loopWalkieTalk(unsigned long now) {
  int32_t x, y;
  const bool down = touchinput::read(lcd, &x, &y);
  if (down && !g_touchDown) {  // press edge
    g_touchDown = true;
    g_wtStartX = x; g_wtStartY = y;
    if (ui::inWalkieTalkBtn(x, y) && !g_wtHeld) {
      if (walkie.txStart(g_wtTarget)) g_wtHeld = true;
      else audio.playBuzzer();  // mic muted / night mode refuse the claim
    }
  }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;
  } else if (g_touchDown) {    // release edge
    g_touchDown = false;
    lastInteractionMs = now;
    if (g_wtHeld) {
      g_wtHeld = false;
      walkie.txEnd();
    } else {
      const int32_t dx = g_touchX - g_wtStartX, dy = g_touchY - g_wtStartY;
      if (ui::isBackPull(dx, dy, g_wtStartX, g_wtStartY)) {  // left tab/pull
        audio.playClick();
        screen = SCREEN_WALKIE;
      }
    }
  }
  if (walkie.state() != WT_IDLE) lastInteractionMs = now;  // no idle-close mid-talk
  renderer.drawWalkieTalk(walkie, g_wtTargetName, g_wtHeld);
}

// HA control panel: swipe up/down = page, tap a controllable tile = toggle
// (optimistic), left-edge pull / tab = back to the pet. Own touch handling
// (raw), like the games menu.
static int32_t g_haStartX = 0, g_haStartY = 0;
// The panel opens by pulling the LEFT edge RIGHT - the exact same direction as
// its own "back" gesture. Without this guard, the tail of the opening swipe
// (still under the finger when we switch screens) reads as a back and slams the
// panel shut the instant it opens. Swallow every touch until the finger lifts
// once, so the first honored gesture starts clean. (Games escapes this because
// its open-swipe goes the other way.)
static bool g_haSwallow = false;
static unsigned long g_haOpenedMs = 0;  // when the panel was opened (loading UI)
static void loopHaPanel(unsigned long now) {
  // "Loading" = first list hasn't arrived yet and we opened recently (the
  // plugin answers ha:sub in <1s when connected; past the window it's real
  // disconnection and the empty state says so).
  const bool loading = !haPanel.everReceived() && now - g_haOpenedMs < 8000;
  int32_t x, y;
  const bool down = touchinput::read(lcd, &x, &y);
  if (g_haSwallow) {
    lastInteractionMs = now;  // don't let the idle auto-close fire while waiting
    if (down) { renderer.drawHaPanel(haPanel, g_haPage, loading); return; }
    g_haSwallow = false;  // finger lifted: clean slate for real gestures
    g_touchDown = false;
  }
  if (down && !g_touchDown) { g_touchDown = true; g_haStartX = x; g_haStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;
  } else if (g_touchDown) {  // release
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_haStartX, dy = g_touchY - g_haStartY;
    const int pages = (haPanel.count() + ui::HA_PER_PAGE - 1) / ui::HA_PER_PAGE;
    if (abs(dy) > ui::SWIPE_MIN && abs(dy) > abs(dx)) {  // vertical swipe = page
      if (dy < 0 && g_haPage < pages - 1) { g_haPage++; audio.playClick(); }
      else if (dy > 0 && g_haPage > 0)    { g_haPage--; audio.playClick(); }
    } else if (ui::isBackPull(dx, dy, g_haStartX, g_haStartY)) {  // left = back
      audio.playClick();
      screen = SCREEN_PET;
    } else {  // tap a tile -> toggle if controllable (optimistic)
      const int t = ui::haTileAt(g_haStartX, g_haStartY);
      const int idx = g_haPage * ui::HA_PER_PAGE + t;
      if (t >= 0 && idx < haPanel.count()) {
        HaPanel::Entity& e = haPanel.at(idx);
        if (e.controllable) {
          if (!strcmp(e.state, "on")) strcpy(e.state, "off");
          else if (!strcmp(e.state, "off")) strcpy(e.state, "on");
          e.pending = true;
          web.sendHaCmd(e.id, "toggle");
          audio.playClick();
        }
      }
    }
  }
  renderer.drawHaPanel(haPanel, g_haPage, loading);  // idle close: screen table
}

// Weather forecast screen: opened by swiping UP from the bottom of the pet
// screen. Back = left-edge pull / left tab, or swipe DOWN (mirror of the
// opening gesture). Raw touch, same template as the other custom screens.
static int32_t g_wxStartX = 0, g_wxStartY = 0;
static void loopWeather(unsigned long now) {
  int32_t x, y;
  const bool down = touchinput::read(lcd, &x, &y);
  if (down && !g_touchDown) { g_touchDown = true; g_wxStartX = x; g_wxStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;
  } else if (g_touchDown) {  // release
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_wxStartX, dy = g_touchY - g_wxStartY;
    const bool backSwipeDown = dy > ui::SWIPE_MIN && abs(dy) > abs(dx);
    if (backSwipeDown || ui::isBackPull(dx, dy, g_wxStartX, g_wxStartY)) {
      audio.playClick();
      screen = SCREEN_PET;
    }
  }
  renderer.drawWeather(weather);  // idle auto-close lives in the screen table
}

// ---- Screen table ----------------------------------------------------------
// One row per full-screen mode: the loop() dispatcher, the name reported to
// the portal, the game-score source, the frame pacing and whether the idle
// timeout auto-returns to the pet. Adding a screen = one row here + one
// enterScreen case, instead of touching four scattered blocks.
struct ScreenDef {
  Screen id;
  const char* name;             // web.setScreen() value
  void (*loop)(unsigned long);  // per-frame handler
  int (*score)();               // game score for the portal (nullptr = none)
  uint8_t frameDelayMs;         // loop pacing for this screen
  bool idleClose;               // menu timeout returns to SCREEN_PET
};
static const ScreenDef SCREENS[] = {
    {SCREEN_DOODLE, "doodle", loopDoodle, [] { return doodle.score(); }, 12, false},
    // forest backdrop is heavier to redraw; 50fps is plenty
    {SCREEN_BALL, "ball", loopBall, [] { return ball.catches(); }, 20, false},
    {SCREEN_SIMON, "simon", loopSimon, [] { return simon.score(); }, 15, false},
    {SCREEN_GAMES, "games", loopGamesMenu, nullptr, 30, true},
    {SCREEN_HA, "ha", loopHaPanel, nullptr, 30, true},
    {SCREEN_WEATHER, "weather", loopWeather, nullptr, 30, true},
    {SCREEN_WALKIE, "walkie", loopWalkieList, nullptr, 30, true},
    // Talk screen idle-closes back to the pet after the menu timeout - but
    // never mid-conversation (the loop refreshes lastInteraction during
    // TX/RX), and an incoming PTT re-opens it automatically.
    {SCREEN_WALKIE_TALK, "walkietalk", loopWalkieTalk, nullptr, 20, true},
};
static const ScreenDef* screenDef(Screen s) {
  for (const auto& d : SCREENS)
    if (d.id == s) return &d;
  return nullptr;  // SCREEN_PET (handled inline in loop())
}

// Everything a screen needs on entry, in ONE place - touch, serial console
// and the portal all funnel here. The per-call-site copies had drifted
// (serial nav to the HA panel skipped haSubscribe, for instance).
static void enterScreen(Screen s, unsigned long now) {
  menuOpen = false;
  touchinput::cancel();  // no ghost taps: drop any synthetic-touch leftovers
  switch (s) {
    case SCREEN_HA:
      g_haPage = 0;
      g_haOpenedMs = now;
      g_haSwallow = true;
      web.haSubscribe();
      break;
    case SCREEN_WEATHER:
      weather.requestFetch(15UL * 60 * 1000);  // refresh if >15 min old
      break;
    case SCREEN_DOODLE: startDoodle(now); return;  // sets screen itself
    case SCREEN_SIMON:  startSimon(now);  return;  // sets screen itself
    case SCREEN_BALL:
      ball.reset();
      g_touchDown = false;
      break;
    case SCREEN_WALKIE:
      walkie.requestScan();  // fresh roster as the list opens
      g_touchDown = false;
      break;
    case SCREEN_WALKIE_TALK:
      g_wtHeld = false;
      g_touchDown = false;
      break;
    default: break;  // SCREEN_PET / SCREEN_GAMES: nothing extra
  }
  screen = s;
}

// Navigation/action commands from the DebugConsole (screen state lives here;
// module-level commands like vol:/led:/stats: are handled inside the console).
static bool consoleNavigate(const String& c) {
  const unsigned long now = millis();
  if (c == "shot")   { g_shotPending = true; return true; }  // captured post-render
  if (c.startsWith("voice:")) { g_voiceDebug = c.substring(6).toInt(); return true; }  // force voice ring
  if (c == "diag") {  // runtime health snapshot (the "black box" counterpart)
    Serial.printf("[diag] uptime %lus | reset %s\n", now / 1000,
                  resetReasonName(esp_reset_reason()));
    Serial.printf("[diag] heap %uKB free (min %uKB, largest %uKB) | psram %uKB free\n",
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getMinFreeHeap() / 1024),
                  (unsigned)(ESP.getMaxAllocHeap() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
    Serial.printf("[diag] wifi %s rssi %d | ws clients %d | loop stack %u free\n",
                  WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "down",
                  WiFi.RSSI(), web.authedCount(),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    return true;
  }
  if (c == "token")  { Serial.printf("[auth] pairing token: %s\n", web.authToken()); return true; }
  if (c == "pair")   { web.startPairing(); Serial.printf("[auth] pairing pin: %s\n", web.pairingPin()); return true; }
  if (c == "revoke") { web.revokeAll(); Serial.println("[auth] revoked (all clients)"); return true; }
  if (c == "ha") {  // dump the HA panel model
    Serial.printf("[ha] %d entities (stale %lums):\n", haPanel.count(), haPanel.staleMs());
    for (int i = 0; i < haPanel.count(); i++) {
      const auto& e = haPanel.at(i);
      Serial.printf("  %-40s d=%-8s s=%-6s v=%-8s %s\n", e.id, e.domain, e.state, e.value,
                    e.controllable ? "[ctrl]" : "");
    }
    return true;
  }
  if (c.startsWith("hatoggle:")) {  // debug: toggle entity by index
    const int i = c.substring(9).toInt();
    if (i >= 0 && i < haPanel.count()) {
      web.sendHaCmd(haPanel.at(i).id, "toggle");
      Serial.printf("[ha] toggle -> %s\n", haPanel.at(i).id);
    }
    return true;
  }
  if (c == "pet")    { enterScreen(SCREEN_PET, now); return true; }
  if (c == "games")  { enterScreen(SCREEN_GAMES, now); return true; }
  if (c == "hapanel"){ enterScreen(SCREEN_HA, now); return true; }
  if (c == "walkie") { enterScreen(SCREEN_WALKIE, now); return true; }
  if (c == "walkietalk") { enterScreen(SCREEN_WALKIE_TALK, now); return true; }
  if (c == "weather"){ enterScreen(SCREEN_WEATHER, now); return true; }
  if (c == "wx") {  // dump the weather model
    Serial.printf("[wx] city='%s' loc=%d fetched=%d stale=%lums now=%dC code=%d kind=%d\n",
                  weather.city(), weather.hasLocation(), weather.everFetched(),
                  weather.staleMs(), weather.tempNow(), weather.codeNow(),
                  (int)weather.kindNow());
    for (int i = 0; i < weather.dayCount(); i++) {
      const auto& d = weather.day(i);
      Serial.printf("  %s code=%d hi=%d lo=%d pop=%d\n", d.day, d.code, d.hi, d.lo, d.pop);
    }
    return true;
  }
  if (c == "wxfetch") { weather.requestFetch(0); Serial.println("[wx] fetch requested"); return true; }
  if (c.startsWith("wxset:")) {  // force a scene WMO code by name or number
    g_wxDebug = Weather::codeFromName(c.substring(6).c_str());
    Serial.printf("[wx] forced code = %d\n", g_wxDebug);
    web.setWxDebug(g_wxDebug);  // echo to the portal (survives refresh)
    return true;
  }
  if (c.startsWith("fest:")) {  // force festive art: natal|halloween|junina|bday|off|auto
    const String f = c.substring(5);
    g_festDebug = f == "natal"     ? Renderer::FEST_NATAL
                : f == "halloween" ? Renderer::FEST_HALLOWEEN
                : f == "junina"    ? Renderer::FEST_JUNINA
                : (f == "nye" || f == "anonovo") ? Renderer::FEST_NYE
                : f == "bday"      ? 9
                : f == "off"       ? Renderer::FEST_NONE
                                   : -1;  // auto
    Serial.printf("[fest] debug=%d\n", g_festDebug);
    web.setFestDebug(g_festDebug);  // echo to the portal (survives refresh)
    // apply immediately (loopFestive is throttled to 1/min)
    renderer.setFestive(
        g_festDebug >= 0 && g_festDebug != 9 ? (Renderer::Fest)g_festDebug
                                             : Renderer::FEST_NONE,
        g_festDebug == 9);
    return true;
  }
  if (c.startsWith("bday:")) {  // set birthday: "YYYY-MM-DD" or legacy "MM-DD"
    const String d = c.substring(5);
    const bool full = d.length() == 10 && d[4] == '-' && d[7] == '-';
    const bool legacy = d.length() == 5 && d[2] == '-';
    if (full || legacy) {
      strlcpy(g_bdayDate, d.c_str(), sizeof(g_bdayDate));
      Preferences p;
      p.begin("pet", false);
      p.putString("bday", g_bdayDate);
      p.end();
      web.setBirthday(g_bdayDate);
      Serial.printf("[bday] aniversario do bichinho: %s\n", g_bdayDate);
    }
    return true;
  }
  if (c == "bday") { Serial.printf("[bday] %s\n", g_bdayDate); return true; }
  if (c == "wt") { walkie.printStats(); return true; }
  if (c == "wt:scan") { walkie.requestScan(); return true; }
  if (c == "wt:on") { walkie.setEnabled(true); return true; }
  if (c == "wt:off") { walkie.setEnabled(false); return true; }
  if (c.startsWith("wt:tx:")) {  // debug: TX to a raw IP without the UI/touch
    // wt:tx:bc = broadcast; wt:tx:<n> = peer index; used by validation scripts
    const String t = c.substring(6);
    bool ok;
    if (t == "bc") ok = walkie.txStart(-1);
    else ok = walkie.txStart(t.toInt());
    Serial.printf("[wt] txStart -> %s\n", ok ? "ok" : "REFUSED");
    return true;
  }
  if (c == "wt:txstop") { walkie.txEnd(); return true; }
  if (c == "wxbolt") {  // debug: strike lightning right now (bolt + thunder)
    renderer.triggerBolt();
    return true;
  }
  if (c == "mix") {  // debug: thunder via the mixer (overlay if busy, decoder if idle)
    const bool wasBusy = audio.busy();
    audio.playThunder();
    Serial.printf("[mix] thunder fired (busy=%d overlay=%d)\n", wasBusy,
                  AudioCodec::overlayActive());
    return true;
  }
  if (c.startsWith("wxloc:")) {  // wxloc:<lat>,<lon>[,<city>]
    const String rest = c.substring(6);
    const int c1 = rest.indexOf(','), c2 = rest.indexOf(',', c1 + 1);
    if (c1 > 0) {
      const float lat = rest.substring(0, c1).toFloat();
      const float lon = (c2 > 0 ? rest.substring(c1 + 1, c2) : rest.substring(c1 + 1)).toFloat();
      const String city = c2 > 0 ? rest.substring(c2 + 1) : String("Local");
      weather.setLocation(lat, lon, city.c_str());
      Serial.printf("[wx] location set: %.4f,%.4f '%s'\n", lat, lon, weather.city());
    }
    return true;
  }
  if (c == "doodle") { enterScreen(SCREEN_DOODLE, now); return true; }
  if (c == "ball")   { enterScreen(SCREEN_BALL, now); return true; }
  if (c == "simon")  { enterScreen(SCREEN_SIMON, now); return true; }

  if (c.startsWith("menu")) {
    screen = SCREEN_PET; menuOpen = true;
    const String pg = c.substring(4);  // "" or ":audio"/":luz"/":conn"/":qr"/":seg"/":ha"/":token"
    menuPage = pg == ":audio" ? ui::PAGE_AUDIO
             : pg == ":luz"   ? ui::PAGE_LIGHT
             : pg == ":conn"  ? ui::PAGE_CONN
             : pg == ":qr"    ? ui::PAGE_QR
             : pg == ":seg"   ? ui::PAGE_SEC
             : pg == ":ha"    ? ui::PAGE_SEC_HA
                              : ui::PAGE_MAIN;
    return true;
  }

  if (c == "feed")  { doAction(ACTION_FEED);  return true; }
  if (c == "pat")   { doAction(ACTION_PAT);   return true; }
  if (c == "clean") { doAction(ACTION_CLEAN); return true; }
  if (c == "sleep") { doAction(ACTION_TOGGLE_SLEEP); return true; }
  return false;
}

// ---- loop() subsystems -----------------------------------------------------
// Each of these was an anonymous block inside loop(); they keep their state in
// function-local statics and run once per frame, in the order loop() calls
// them. Extracting them keeps loop() readable orchestration.

// Weather -> scene/SFX: adopt finished fetches, honor the dev-panel override,
// paint the scene condition, fire thunder, and schedule the ambient clips
// (rain patter / forest wind every 60-120s, pet screen only, never over
// other audio - the play helpers bail if busy).
static void loopWeatherFx(unsigned long now) {
  weather.loop();
  {  // dev panel (portal) can force/clear the scene weather like the serial
    const int req = web.consumeWxSet();
    if (req != -2) g_wxDebug = req;
  }
  web.setWxDebug(g_wxDebug);  // keep the portal selector in sync (no-op if same)
  const uint8_t wxCode =
      g_wxDebug >= 0 ? (uint8_t)g_wxDebug
                     : (weather.fresh() ? weather.codeNow() : 0);
  renderer.setWeather(wxCode,
                      weather.fresh() ? (int8_t)weather.tempNow() : INT8_MIN);
  if (renderer.consumeThunder()) audio.playThunder();  // strike -> clap
  static unsigned long ambientAt = 0;
  static uint8_t lastFam = 255;
  const WxKind fam = Weather::kindFromCode(wxCode);
  if ((uint8_t)fam != lastFam) { lastFam = (uint8_t)fam; ambientAt = now + 8000; }
  const bool rainy = fam == WX_RAIN || fam == WX_STORM;
  const bool windy = fam == WX_FOG || fam == WX_SNOW;
  if ((rainy || windy) && screen == SCREEN_PET && !g_fullSleep &&
      now >= ambientAt) {
    ambientAt = now + 60000 + (esp_random() % 60000);
    if (rainy) audio.playRainAmb();
    else audio.playWindAmb();
  }
}

// Festive dates -> scene decorations + sparse themed ambience. Real calendar
// (needs a synced clock; without it nothing shows) checked once a minute; the
// fest: debug command forces a look for testing/screenshots (like wxset:).
static void loopFestive(unsigned long now) {
  // Portal dev panel can force a theme (fest:) and set the birthday (bday:).
  const int freq = web.consumeFestSet();
  static unsigned long nextAt = 0;
  if (freq != -2) {
    g_festDebug = freq;
    nextAt = 0;  // apply NOW - waiting up to 60s read as "theme doesn't work"
  }
  web.setFestDebug(g_festDebug);  // keep the portal selector in sync (survives refresh)
  char bd[11];
  if (web.consumeBirthday(bd, sizeof(bd))) {
    strlcpy(g_bdayDate, bd, sizeof(g_bdayDate));
    Preferences p;
    p.begin("pet", false);
    p.putString("bday", g_bdayDate);
    p.end();
    web.setBirthday(g_bdayDate);
    Serial.printf("[bday] aniversario do bichinho: %s\n", g_bdayDate);
  }

  // Flyby sound cue (sleigh "ho ho ho" / witch cackle): the renderer flags
  // the instant the flight starts; consumed EVERY tick (before the 60s
  // throttle) so the sound lands in sync with the visual.
  {
    const uint8_t fly = renderer.consumeFlyby();
    if (fly && screen == SCREEN_PET && !g_fullSleep) {
      if (fly == 1) audio.playHoHoHo();
      else audio.playWitch();
    }
  }

  static int sangOnDay = -1;  // yday we already sang parabens on
  if (now < nextAt) return;
  nextAt = now + 60000;

  Renderer::Fest fest = Renderer::FEST_NONE;
  bool bday = false;
  int today = -1, hour = 12, mon = 0, day = 0;
  if (petClock.synced()) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    today = lt.tm_yday;
    hour = lt.tm_hour;
    const int m = lt.tm_mon + 1, d = lt.tm_mday;
    mon = m; day = d;
    if ((m == 12 && d == 31) || (m == 1 && d == 1)) fest = Renderer::FEST_NYE;
    else if (m == 12 && d <= 25) fest = Renderer::FEST_NATAL;
    else if (m == 10 && d >= 24) fest = Renderer::FEST_HALLOWEEN;
    else if (m == 6 && d >= 12 && d <= 24) fest = Renderer::FEST_JUNINA;
    char md[6];
    snprintf(md, sizeof(md), "%02d-%02d", m, d);
    bday = strcmp(md, bdayMonthDay()) == 0;
  }
  if (g_festDebug >= 0) {  // forced (debug) overrides the calendar
    fest = g_festDebug == 9 ? Renderer::FEST_NONE : (Renderer::Fest)g_festDebug;
    bday = g_festDebug == 9;
  }
  renderer.setFestive(fest, bday);

  // Parabens: once per birthday (and once per forced test).
  if (bday && today != sangOnDay && screen == SCREEN_PET && !g_fullSleep) {
    audio.playBirthday();
    sangOnDay = today;
  }
  if (!bday) sangOnDay = -1;

  // NYE midnight: the barrage fires RIGHT at the turn of the year (first
  // tick of Jan 1st, hour 0), interrupting whatever else is sounding.
  static int nyeBoomDay = -1;
  if (mon == 1 && day == 1 && hour == 0 && today != nyeBoomDay && !g_fullSleep) {
    audio.playNyeFireworks(true);
    nyeBoomDay = today;
  }

  // Sparse themed ambience on the pet screen: bells / owl / fireworks every
  // 2.5-5 min (the play helpers bail if anything else is sounding). The owl
  // only hoots after dark, like a proper owl.
  static unsigned long ambientAt = 0;
  if (fest != Renderer::FEST_NONE && screen == SCREEN_PET && !g_fullSleep &&
      now >= ambientAt) {
    ambientAt = now + 150000 + (esp_random() % 150000);
    if (fest == Renderer::FEST_NATAL) audio.playXmasBells();
    else if (fest == Renderer::FEST_JUNINA) audio.playFireworks();
    else if (fest == Renderer::FEST_NYE) audio.playNyeFireworks();
    else if (fest == Renderer::FEST_HALLOWEEN &&
             (g_festDebug >= 0 || hour >= 18 || hour < 6)) {
      audio.playOwl();
    }
  }
}

// Full sleep (night mode, for HA schedule automations): screen + LED dark,
// The pet asleep, mic REALLY muted (visible in the HA switch); any local touch
// or BOOT press wakes everything back up and restores the pre-night mute.
static void loopNightMode(unsigned long now) {
  int req = web.consumeFullSleep();
  if (g_fullSleep) {
    int32_t tx, ty;
    if (lcd.getTouch(&tx, &ty) || digitalRead(PIN_BOOT_BUTTON) == LOW) req = 0;
  }
  if (req == 1 && !g_fullSleep) {
    g_fullSleep = true;
    menuOpen = false;
    screen = SCREEN_PET;  // leave any game
    if (!pet.sleeping()) {
      g_muteNextSleepSnd = !g_nightSleepSnd;  // night mode may be silent
      doAction(ACTION_TOGGLE_SLEEP);          // tuck the pet in
    }
    led.gameOff();               // LED dark (override until wake)
    renderer.setDisplayOff(true);
    web.setFullSleep(true);
    walkie.setFullSleep(true);   // walkie RX muted during night mode
    g_micMutedBeforeNight = web.micMuted();
    if (!g_micMutedBeforeNight) web.setMicMuted(true);
  } else if (req == 0 && g_fullSleep) {
    g_fullSleep = false;
    renderer.setDisplayOff(false);
    led.endGame();               // release the LED back to mood
    if (pet.sleeping()) {
      g_muteNextWakeSnd = !g_nightWakeSnd;
      doAction(ACTION_TOGGLE_SLEEP);  // wake the pet
    }
    if (!g_micMutedBeforeNight && web.micMuted()) web.setMicMuted(false);
    web.setFullSleep(false);
    walkie.setFullSleep(false);
    lastInteractionMs = now;
    g_inputSwallowUntil = now + 800;  // the wake tap must not also feed/pat
  }

  // Night sound settings changed from HA/portal: apply + persist.
  int ss, ws;
  if (web.consumeNightSnd(ss, ws)) {
    if (ss >= 0) g_nightSleepSnd = ss;
    if (ws >= 0) g_nightWakeSnd = ws;
    web.setNightSnd(g_nightSleepSnd, g_nightWakeSnd);
    Preferences p;
    p.begin("night", false);
    p.putBool("ssnd", g_nightSleepSnd);
    p.putBool("wsnd", g_nightWakeSnd);
    p.end();
  }
}

// Pairing overlay: mirror the PIN (renderer takes over the screen while it's
// set) and make sure we're on the pet screen so it actually shows. While it's
// up, the ONLY live touch target is the cancel X - everything else is
// swallowed (taps must not invisibly feed/pet the hidden scene).
static void loopPairingOverlay(unsigned long now) {
  renderer.setPairingPin(web.pairingActive() ? web.pairingPin() : "");
  if (!web.pairingActive()) return;
  if (screen != SCREEN_PET) { screen = SCREEN_PET; menuOpen = false; }
  int32_t tx, ty;
  if (lcd.getTouch(&tx, &ty) && ui::inPairCancel(tx, ty)) {
    web.cancelPairing();
    audio.playClick();
    g_inputSwallowUntil = now + 800;  // the cancel tap ends here
  }
}

// Media LED show: beat-following rainbow while music plays ("balada"), cyan
// pulse while the assistant speaks (matches the on-screen voice ring). Uses
// the same override channel as the Genius game; released when media ends.
static void loopMediaLedShow(unsigned long now) {
  static uint8_t lastKind = 0;
  const uint8_t mk = (uint8_t)audio.mediaKind();
  if (mk == AudioPlayer::MEDIA_MUSIC) {
    // Beat-follow: the CodecOutput reports the live PCM envelope. A spike
    // above the ~1s running average = beat -> hue jump + full-bright flash
    // that decays; between beats the LED holds a dim glow of the current
    // color. If the track has no clear transients, the hue still drifts.
    static float slowEnv = 40.0f, flash = 0.0f;
    static uint8_t hue = 0;
    static unsigned long lastBeatMs = 0;
    const float lvl = (float)audio.mediaLevel();       // 0..255
    slowEnv += (lvl - slowEnv) * 0.03f;                // ~1s average @33fps
    if (lvl > slowEnv * 1.35f + 6.0f && now - lastBeatMs > 160) {
      lastBeatMs = now;
      flash = 1.0f;
      hue += 47;                                       // new color per beat
    } else {
      flash *= 0.86f;                                  // fast decay
    }
    if (now - lastBeatMs > 1800) hue += 2;             // beatless: slow drift
    const uint8_t x = (hue % 85) * 3;
    uint8_t r, g, b;
    if (hue < 85)       { r = 255 - x; g = x;       b = 0; }
    else if (hue < 170) { r = 0;       g = 255 - x; b = x; }
    else                { r = x;       g = 0;       b = 255 - x; }
    const float k = 0.25f + 0.75f * flash;             // dim glow <-> flash
    led.gameColor((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
  } else if (mk == AudioPlayer::MEDIA_TTS) {
    const uint8_t p = (uint8_t)(120 + 100 * sinf(now / 300.0f));
    led.gameColor(0, p / 2, p);
  } else if (lastKind) {
    led.endGame();  // media over: LED back to the mood
  }
  lastKind = mk;
}

// Voice assistant feedback state machine (drives the on-screen ring + LED +
// portal). listening (BOOT held) -> thinking (released, STT/intent) ->
// speaking (TTS plays) -> idle. The thinking phase closes the feedback gap
// between releasing the button and hearing the reply; a timeout clears it if
// no answer comes (e.g. STT heard nothing). Returns the current state for
// the renderer: 0 idle, 1 listening, 2 thinking, 3 speaking.
static uint8_t loopVoiceFsm(unsigned long now, const InputEvent& ev) {
  static uint8_t g_voice = 0, g_voiceLast = 0;
  static unsigned long g_voiceSince = 0;
  if (ev.voice == InputEvent::V_PTT_START) {
    lastInteractionMs = now;
    if (!web.micMuted()) {  // muted: PTT is denied (red LED already says why)
      web.voicePttStart(); g_voice = 1; g_voiceSince = now;
    }
  } else if (ev.voice == InputEvent::V_PTT_END) {
    if (g_voice == 1) { web.voicePttEnd(); g_voice = 2; g_voiceSince = now; }
  } else if (ev.voice == InputEvent::V_MUTE_TOGGLE) {
    // Quick BOOT tap: privacy mute, Echo-style. Down chime = mic closed,
    // up chime = mic open; the LED stays red while muted.
    lastInteractionMs = now;
    const bool muted = !web.micMuted();
    web.setMicMuted(muted);
    if (muted) audio.playConfirm(); else audio.playListen();
  }
  // HA-driven states (wake word pipeline pushes voice:listening/thinking/...).
  const int vc = web.consumeVoiceCmd();
  if (vc >= 0) { g_voice = (uint8_t)vc; g_voiceSince = now; }
  if (audio.mediaKind() == AudioPlayer::MEDIA_TTS) {
    g_voice = 3; g_voiceSince = now;                     // reply is speaking
  } else if (g_voice == 3) {
    g_voice = 0;                                         // TTS finished
  } else if (g_voice != 0 && now - g_voiceSince > 15000) {
    g_voice = 0;  // stuck without progress (e.g. HA died mid-run): clear
  }
  if (g_voiceDebug >= 0) g_voice = (uint8_t)g_voiceDebug;  // debug: force a ring
  if (g_voice != g_voiceLast) {
    static const char* const NAMES[] = {"idle", "listening", "thinking", "speaking"};
    web.setVoiceState(NAMES[g_voice]);
    switch (g_voice) {
      case 1: led.gameColor(0, 200, 255); break;   // cyan
      case 2: led.gameColor(255, 150, 0); break;   // amber
      case 3: led.gameColor(0, 200, 255); break;   // cyan
      default: led.endGame(); break;               // release to mood
    }
    g_voiceLast = g_voice;
  }
  return g_voice;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[homecritters] boot (reset: %s, heap %uKB, psram %uKB)\n",
                resetReasonName(esp_reset_reason()),
                (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)(ESP.getFreePsram() / 1024));

  // Task watchdog, 30s: every long-running task subscribes itself and feeds it
  // (audio decoder, weather, http, miccap, audioread, and this loop below).
  // The idle-task WDTs stay unwatched (see AudioPlayer::begin) - busy audio
  // legitimately starves idle; a task that stops feeding for 30s is a real
  // hang and the panic handler reboots us out of it.
  esp_task_wdt_init(30, true);

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
  web.begin(&pet, &audio, &led, &petClock, &renderer, &haPanel, doAction);  // WiFi + portal
  walkie.begin(&audio, &web);  // UDP walkie-talkie (discovery via mDNS)
  web.setWalkie(&walkie);      // wt/wt:* over WS (portal + scripted E2E)
  web.setBattery(battery.percent());  // seed the portal value
  web.setWeatherModel(&weather);      // wxloc command + wxCity in the state
  // Weather fetch task (core 0); skips fetches while media streams.
  weather.begin([]() { return audio.streaming(); });
  console.begin(&pet, &battery, &audio, &led, &renderer, consoleNavigate,
                []() { lastInteractionMs = millis(); });

  // The pet's birthday (persisted; default = the project's first commit day).
  {
    Preferences p;
    p.begin("pet", true);
    String b = p.getString("bday", "2026-07-09");  // full ISO; legacy MM-DD ok
    p.end();
    strlcpy(g_bdayDate, b.c_str(), sizeof(g_bdayDate));
    web.setBirthday(g_bdayDate);  // seed the portal/HA field
  }

  // Night-mode sound settings (persisted).
  {
    Preferences p;
    p.begin("night", true);
    g_nightSleepSnd = p.getBool("ssnd", true);
    g_nightWakeSnd = p.getBool("wsnd", true);
    p.end();
    web.setNightSnd(g_nightSleepSnd, g_nightWakeSnd);
  }

  wasSleeping = pet.sleeping();
  lastTickMs = lastSaveMs = lastInteractionMs = millis();
  esp_task_wdt_add(nullptr);  // watch the render loop too
  taskreg::add("loopTask", xTaskGetCurrentTaskHandle(), 1, 1);  // `top` roster
  Serial.printf("[homecritters] ready. battery ~%d%%\n", battery.percent());
}

void loop() {
  esp_task_wdt_reset();
  const unsigned long now = millis();

  console.poll();  // debug console (screenshots + navigation)
  // Weather: adopt fetches, paint the scene condition, thunder + ambient SFX.
  // Runs on every screen so the data never goes stale.
  loopWeatherFx(now);

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
      if (ui::isBackPull(dx, dy, wcStartX, wcStartY)) {
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
    if (g_muteNextSleepSnd) g_muteNextSleepSnd = false;  // silent night mode
    else audio.playSleepTune();
  } else if (!sleeping && wasSleeping) {
    if (g_muteNextWakeSnd) g_muteNextWakeSnd = false;
    else audio.playWake();
  }
  wasSleeping = sleeping;

  loopFestive(now);         // seasonal decorations + the pet's hat (real date)
  loopNightMode(now);       // full-sleep enter/exit + night sound settings
  loopPairingOverlay(now);  // PIN overlay mirror + cancel X

  if (now - lastSaveMs > game::SAVE_INTERVAL_MS) {
    pet.save();
    lastSaveMs = now;
  }
  // Debounced settings persists (volume / LED / screen brightness): the
  // sliders apply instantly in RAM, flash gets ONE write 2s after the drag.
  audio.flushNvs();
  led.flushNvs();
  renderer.flushNvs();

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

  // The WebSocket must keep running during playback: Music Assistant tears the
  // stream down if the device's WS goes silent (heartbeat timeout). Broadcast
  // volume is already minimized while streaming (animation mirror suppressed).
  web.handle();
  walkie.pumpTx();     // TX: drain mic frames -> UDP (no-op when idle)
  walkie.tick(now);    // session timeouts + radio keepalive
  {  // Incoming PTT pulls the UI to the talk screen (Apple Watch behavior):
     // whoever called becomes the reply target.
    static WtState wtUiPrev = WT_IDLE;
    const WtState ws = walkie.state();
    if (ws == WT_RX && wtUiPrev != WT_RX && screen != SCREEN_WALKIE_TALK &&
        !g_fullSleep) {
      g_wtTarget = walkie.rxPeerIndex();  // -1 -> reply as broadcast
      strlcpy(g_wtTargetName, walkie.rxName(), sizeof(g_wtTargetName));
      enterScreen(SCREEN_WALKIE_TALK, now);
      lastInteractionMs = now;
      web.pushState();
    }
    wtUiPrev = ws;
  }
  {  // portal wt:* commands (scan/on/off/tx:N|bc/txstop)
    char wc[12];
    if (web.consumeWalkieCmd(wc, sizeof(wc))) {
      if (!strcmp(wc, "scan")) walkie.requestScan();
      else if (!strcmp(wc, "on")) walkie.setEnabled(true);
      else if (!strcmp(wc, "off")) walkie.setEnabled(false);
      else if (!strcmp(wc, "txstop")) walkie.txEnd();
      else if (!strncmp(wc, "tx:", 3))
        walkie.txStart(!strcmp(wc + 3, "bc") ? -1 : atoi(wc + 3));
    }
  }

  ferret.update(pet, now);
  // (The portal used to re-render the ferret from a broadcast of anim/pos;
  // that's gone - the portal shows the real screen via the live stream now.)

  led.update(pet.mood());
  loopMediaLedShow(now);  // balada rainbow / TTS cyan pulse
  {  // walkie LED: TX = steady amber, RX = green pulse (any screen). Last
     // gameColor of the frame wins, so this sits after the media show.
    static WtState wtLedLast = WT_IDLE;
    const WtState ws = walkie.state();
    if (ws == WT_TX) led.gameColor(255, 150, 0);
    else if (ws == WT_RX) {
      const uint8_t p = (uint8_t)(110 + 90 * sinf(now / 220.0f));
      led.gameColor(0, p, 40);
    } else if (wtLedLast != WT_IDLE) led.endGame();
    wtLedLast = ws;
  }

  // Report the current screen to the portal and honor phone game nav (start/
  // back), so Doodle Jump can be launched and steered entirely from the phone.
  {
    const ScreenDef* def = screenDef(screen);
    web.setScreen(def ? def->name : "pet");
    switch (web.consumeGameNav()) {
      case WebPortal::NAV_START:
        if (screen != SCREEN_DOODLE) { enterScreen(SCREEN_DOODLE, now); web.pushState(); }
        break;
      case WebPortal::NAV_BALL:
        if (screen != SCREEN_BALL) { enterScreen(SCREEN_BALL, now); web.pushState(); }
        break;
      case WebPortal::NAV_SIMON:
        if (screen != SCREEN_SIMON) { enterScreen(SCREEN_SIMON, now); web.pushState(); }
        break;
      case WebPortal::NAV_BACK:
        if (screen == SCREEN_DOODLE) { leaveDoodle(); web.pushState(); }
        else if (screen == SCREEN_BALL) { leaveBall(now); web.pushState(); }
        else if (screen == SCREEN_SIMON) { leaveSimon(now); web.pushState(); }
        break;
      default: break;
    }
  }

  // --- table-driven full screens (everything except the pet scene) ---
  if (const ScreenDef* def = screenDef(screen)) {
    if (def->score) web.setGameScore(def->score());
    def->loop(now);
    if (def->idleClose) {
      const int timeout = petClock.menuTimeoutSec();
      if (timeout > 0 && screen == def->id &&
          idleSince(now, lastInteractionMs) > (unsigned long)timeout * 1000) {
        screen = SCREEN_PET;
      }
    }
    serviceShots();
    delay(def->frameDelayMs);
    return;
  }

  // --- pet screen ---
  const bool clockActive = petClock.enabled() && petClock.synced() && !menuOpen &&
                           (idleSince(now, lastInteractionMs) > (unsigned long)petClock.idleSec() * 1000);

  InputEvent ev = input.poll(lcd, menuOpen, menuPage);
  // Full sleep, pairing overlay, or just woken by a tap: discard the event -
  // the hidden scene must not react to invisible touches.
  if (g_fullSleep || web.pairingActive() || now < g_inputSwallowUntil) ev = InputEvent{};

  // Voice assistant ring/LED state machine (PTT + wake-word driven).
  const uint8_t voiceState = loopVoiceFsm(now, ev);

  if (ev.ui != ui::UI_NONE || ev.action != ACTION_NONE) {
    lastInteractionMs = now;
    if (clockActive) {
      // in clock mode a touch only wakes the screen (doesn't run the action)
    } else if (ev.ui == ui::UI_HA_TOGGLE) {
      audio.playClick();
      if (!menuOpen) enterScreen(SCREEN_HA, now);
    } else if (ev.ui == ui::UI_GAMES_TOGGLE) {
      audio.playClick();
      if (!menuOpen) enterScreen(SCREEN_GAMES, now);
    } else if (ev.ui == ui::UI_WEATHER_TOGGLE) {
      audio.playClick();
      if (!menuOpen) enterScreen(SCREEN_WEATHER, now);
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

  // Party mode: while music streams, the pet keeps wandering the floor as usual
  // but jumps more often than normal (reuses the pat-jump animation). The
  // interval leaves room for full walk cycles between hops.
  static unsigned long nextHopMs = 0;
  if (audio.mediaKind() == AudioPlayer::MEDIA_MUSIC && !menuOpen && now >= nextHopMs) {
    ferret.onPat();
    nextHopMs = now + 2500 + (esp_random() % 2500);
  }

  // The scene renders normally during media playback. (A render freeze lived
  // here while hunting the streaming stutter - the real culprit turned out to
  // be MCLK EMI on GPIO0, not the render. Measured after that fix: full render
  // + streaming = ring full, 0 underruns, WiFi 300+ KB/s.)
  // The IP string is only rendered inside the config menu; skip the per-frame
  // String allocation otherwise.
  String ip;
  if (menuOpen && web.connected()) ip = web.ip();
  // Seguranca > Aparelhos page: refresh the client list once a second and
  // mirror the revoke button's confirm state (auto-disarms after 4s).
  if (menuOpen && menuPage == ui::PAGE_SEC_HA) {
    static unsigned long lastCli = 0;
    if (now - lastCli > 1000) {
      lastCli = now;
      char ci[120];
      web.clientsInfo(ci, sizeof(ci));
      renderer.setClientsInfo(ci);
    }
    renderer.setRevokeArmed(now < g_revokeArmedUntil);
  }
  renderer.draw(pet, battery, ferret, menuOpen, menuPage, audio.volume(),
                led.brightness(), web.connected(), ip.c_str(), clockActive, petClock,
                (uint8_t)audio.mediaKind(), voiceState, web.micMuted(), web.micLive());
  serviceShots();
  // Faster frames while the portal mirror is watching (the stream's FPS is
  // capped by this loop's rate); relaxed pace when nobody is.
  delay(web.screenViewerActive() ? 15 : 30);
}
