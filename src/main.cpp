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

// Doodle Jump screen: horizontal control from touch, back to the menu on
// the corner button or on game over.
static void loopDoodle(unsigned long now) {
  int32_t x, y;
  const bool down = lcd.getTouch(&x, &y);
  float control = 0.0f;
  if (down && !ui::inGameBack(x, y)) {
    control = constrain((x - 120) / 90.0f, -1.0f, 1.0f);
  }
  doodle.update(now, control);

  if (down) { g_touchDown = true; g_touchX = x; g_touchY = y; }
  else if (g_touchDown) {
    g_touchDown = false;
    if (doodle.gameOver() || ui::inGameBack(g_touchX, g_touchY)) screen = SCREEN_GAMES;
  }
  renderer.drawDoodle(doodle);
}

// Games menu screen: tap a game to start, or Back to the pet scene.
static void loopGamesMenu() {
  int32_t tx, ty;
  if (tapReleased(tx, ty)) {
    if (ui::inGameDoodle(tx, ty)) { doodle.reset(); screen = SCREEN_DOODLE; }
    else if (ui::inGamesBack(tx, ty)) screen = SCREEN_PET;
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
  web.begin(&pet, &audio, &ferret, &petClock, doAction);  // WiFi + portal

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

  // --- game screens ---
  if (screen == SCREEN_DOODLE) {
    loopDoodle(now);
    delay(12);
    return;
  }
  if (screen == SCREEN_GAMES) {
    loopGamesMenu();
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

  String ip = web.connected() ? web.ip() : String();
  renderer.draw(pet, battery, ferret, menuOpen, audio.volume(), web.connected(),
                ip.c_str(), clockActive, petClock);

  delay(30);
}
