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
#include "HaPanel.h"
#include "DebugConsole.h"
#include "pins.h"  // BOOT pin (full-sleep wake check)
#include <Preferences.h>  // night-mode sound settings

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
DebugConsole     console;

// Which screen is showing.
enum Screen { SCREEN_PET, SCREEN_GAMES, SCREEN_DOODLE, SCREEN_BALL, SCREEN_SIMON, SCREEN_HA };
static Screen screen = SCREEN_PET;
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

static bool g_shotPending = false;  // serial "shot": capture after the next render
static int g_voiceDebug = -1;       // serial "voice:N": force a voice ring (-1 = off)
static bool g_fullSleep = false;    // night mode: screen+LED dark, pet asleep
static unsigned long g_inputSwallowUntil = 0;  // discard input right after wake
// Night-mode sound settings (NVS): play the snore/wake tune on FULL-sleep
// transitions? The regular sleep button always keeps its sounds.
static bool g_nightSleepSnd = true, g_nightWakeSnd = true;
static bool g_muteNextSleepSnd = false, g_muteNextWakeSnd = false;
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

// HA control panel: swipe up/down = page, tap a controllable tile = toggle
// (optimistic), left-edge pull / tab = back to the pet. Own touch handling
// (raw), like the games menu.
static int32_t g_haStartX = 0, g_haStartY = 0;
static void loopHaPanel(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);
  if (down && !g_touchDown) { g_touchDown = true; g_haStartX = x; g_haStartY = y; }
  if (down) {
    g_touchX = x; g_touchY = y;
    lastInteractionMs = now;
  } else if (g_touchDown) {  // release
    g_touchDown = false;
    lastInteractionMs = now;
    const int32_t dx = g_touchX - g_haStartX, dy = g_touchY - g_haStartY;
    const int pages = (haPanel.count() + ui::HA_PER_PAGE - 1) / ui::HA_PER_PAGE;
    if (abs(dy) > 45 && abs(dy) > abs(dx)) {           // vertical swipe = page
      if (dy < 0 && g_haPage < pages - 1) { g_haPage++; audio.playClick(); }
      else if (dy > 0 && g_haPage > 0)    { g_haPage--; audio.playClick(); }
    } else if ((dx > 45 && abs(dx) > abs(dy) && g_haStartX < 65) ||
               ui::inLeftHandle(g_haStartX, g_haStartY)) {  // left = back
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
          audio.playPat();
        }
      }
    }
  }
  const int timeout = petClock.menuTimeoutSec();
  if (timeout > 0 && screen == SCREEN_HA &&
      idleSince(now, lastInteractionMs) > (unsigned long)timeout * 1000) {
    screen = SCREEN_PET;
  }
  renderer.drawHaPanel(haPanel, g_haPage);
}

// Navigation/action commands from the DebugConsole (screen state lives here;
// module-level commands like vol:/led:/stats: are handled inside the console).
static bool consoleNavigate(const String& c) {
  const unsigned long now = millis();
  if (c == "shot")   { g_shotPending = true; return true; }  // captured post-render
  if (c.startsWith("voice:")) { g_voiceDebug = c.substring(6).toInt(); return true; }  // force voice ring
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
  if (c == "pet")    { menuOpen = false; screen = SCREEN_PET; return true; }
  if (c == "games")  { menuOpen = false; screen = SCREEN_GAMES; return true; }
  if (c == "hapanel"){ menuOpen = false; g_haPage = 0; screen = SCREEN_HA; return true; }
  if (c == "doodle") { startDoodle(now); return true; }
  if (c == "ball")   { ball.reset(); g_touchDown = false; screen = SCREEN_BALL; return true; }
  if (c == "simon")  { startSimon(now); return true; }

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
  web.begin(&pet, &audio, &led, &ferret, &petClock, &renderer, &haPanel, doAction);  // WiFi + portal
  web.setBattery(battery.percent());  // seed the portal value
  console.begin(&pet, &battery, &audio, &led, &renderer, consoleNavigate,
                []() { lastInteractionMs = millis(); });

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
    if (g_muteNextSleepSnd) g_muteNextSleepSnd = false;  // silent night mode
    else audio.playSleepTune();
  } else if (!sleeping && wasSleeping) {
    if (g_muteNextWakeSnd) g_muteNextWakeSnd = false;
    else audio.playWake();
  }
  wasSleeping = sleeping;

  // --- Full sleep (night mode, for HA schedule automations): screen + LED
  // dark, Leon asleep. Driven by "fullsleep:on|off" (HA switch / portal
  // button); any local touch or BOOT press wakes everything back up.
  {
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
        doAction(ACTION_TOGGLE_SLEEP);          // tuck Leon in
      }
      led.gameOff();               // LED dark (override until wake)
      renderer.setDisplayOff(true);
      web.setFullSleep(true);
    } else if (req == 0 && g_fullSleep) {
      g_fullSleep = false;
      renderer.setDisplayOff(false);
      led.endGame();               // release the LED back to mood
      if (pet.sleeping()) {
        g_muteNextWakeSnd = !g_nightWakeSnd;
        doAction(ACTION_TOGGLE_SLEEP);  // wake Leon
      }
      web.setFullSleep(false);
      lastInteractionMs = now;
      g_inputSwallowUntil = now + 800;  // the wake tap must not also feed/pat
    }
    // Pairing overlay: mirror the PIN (renderer takes over the screen while
    // it's set) and make sure we're on the pet screen so it actually shows.
    // While it's up, the ONLY live touch target is the cancel X - everything
    // else is swallowed (taps must not invisibly feed/pet the hidden scene).
    renderer.setPairingPin(web.pairingActive() ? web.pairingPin() : "");
    if (web.pairingActive()) {
      if (screen != SCREEN_PET) { screen = SCREEN_PET; menuOpen = false; }
      int32_t tx, ty;
      if (lcd.getTouch(&tx, &ty) && ui::inPairCancel(tx, ty)) {
        web.cancelPairing();
        audio.playClick();
        g_inputSwallowUntil = now + 800;  // the cancel tap ends here
      }
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

  // The WebSocket must keep running during playback: Music Assistant tears the
  // stream down if the device's WS goes silent (heartbeat timeout). Broadcast
  // volume is already minimized while streaming (animation mirror suppressed).
  web.handle();

  ferret.update(pet, now);
  // Mirror the pet's animation to the portal only on the pet screen; during a
  // game the portal isn't showing the pet, so skip the extra broadcasts.
  static uint32_t lastSeq = 0xFFFFFFFF;
  static bool lastFlip = false;
  if (ferret.animSeq() != lastSeq || ferret.faceLeft() != lastFlip) {
    lastSeq = ferret.animSeq();
    lastFlip = ferret.faceLeft();
    // Suppress the high-rate animation mirror (~10x/s: JSON build + core-0
    // send + phone re-render) while media is playing. Those broadcasts steal
    // CPU/network from a live audio stream; the portal animation can freeze
    // for the duration. Meaningful pushes (media/stat changes) still go out.
    if (screen == SCREEN_PET && !audio.streaming()) web.pushState();
  }

  led.update(pet.mood());

  // Media LED show: slow rainbow while music plays ("balada"), cyan pulse
  // while the assistant speaks (matches the on-screen voice ring). Uses the
  // same override channel as the Genius game; released when media ends.
  {
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

  // Report the current screen to the portal and honor phone game nav (start/
  // back), so Doodle Jump can be launched and steered entirely from the phone.
  web.setScreen(screen == SCREEN_DOODLE ? "doodle" :
                screen == SCREEN_BALL   ? "ball"   :
                screen == SCREEN_SIMON  ? "simon"  :
                screen == SCREEN_GAMES  ? "games"  :
                screen == SCREEN_HA     ? "ha"     : "pet");
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
  if (screen == SCREEN_HA) {
    loopHaPanel(now);
    serviceShots();
    delay(30);
    return;
  }

  // --- pet screen ---
  const bool clockActive = petClock.enabled() && petClock.synced() && !menuOpen &&
                           (idleSince(now, lastInteractionMs) > (unsigned long)petClock.idleSec() * 1000);

  InputEvent ev = input.poll(lcd, menuOpen, menuPage);
  // Full sleep, pairing overlay, or just woken by a tap: discard the event -
  // the hidden scene must not react to invisible touches.
  if (g_fullSleep || web.pairingActive() || now < g_inputSwallowUntil) ev = InputEvent{};

  // --- Voice assistant feedback state machine (drives the on-screen ring +
  // LED + portal). listening (BOOT held) -> thinking (released, STT/intent) ->
  // speaking (TTS plays) -> idle. The thinking phase closes the feedback gap
  // between releasing the button and hearing the reply; a timeout clears it if
  // no answer comes (e.g. STT heard nothing). 0 idle,1 listen,2 think,3 speak.
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
    // up chime = mic open; the LED stays red while muted (below).
    lastInteractionMs = now;
    const bool muted = !web.micMuted();
    web.setMicMuted(muted);
    if (muted) audio.playConfirm(); else audio.playListen();
  }
  if (audio.mediaKind() == AudioPlayer::MEDIA_TTS) {
    g_voice = 3; g_voiceSince = now;                     // reply is speaking
  } else if (g_voice == 3) {
    g_voice = 0;                                         // TTS finished
  } else if (g_voice == 2 && now - g_voiceSince > 12000) {
    g_voice = 0;                                         // no reply: give up
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

  if (ev.ui != ui::UI_NONE || ev.action != ACTION_NONE) {
    lastInteractionMs = now;
    if (clockActive) {
      // in clock mode a touch only wakes the screen (doesn't run the action)
    } else if (ev.ui == ui::UI_HA_TOGGLE) {
      audio.playClick();
      if (!menuOpen) { g_haPage = 0; screen = SCREEN_HA; web.haSubscribe(); }
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

  // Party mode: while music streams, Leon keeps wandering the floor as usual
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
                (uint8_t)audio.mediaKind(), g_voice, web.micMuted());
  serviceShots();

  delay(30);
}
