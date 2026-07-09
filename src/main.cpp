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

static unsigned long lastTickMs = 0;
static unsigned long lastSaveMs = 0;
static unsigned long lastInteractionMs = 0;  // for clock mode (idle timer)
static bool wasSleeping = false;
static bool menuOpen = false;

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

  // --- stat decay for the elapsed time ---
  const float deltaMin = (now - lastTickMs) / 60000.0f;
  if (deltaMin > 0) {
    pet.update(deltaMin);
    lastTickMs = now;
  }

  // --- clock mode: active when enabled, synced and idle ---
  petClock.update(web.connected());
  const bool clockActive = petClock.enabled() && petClock.synced() && !menuOpen &&
                           (now - lastInteractionMs > (unsigned long)petClock.idleSec() * 1000);

  // --- input (touch / BOOT) ---
  InputEvent ev = input.poll(lcd, menuOpen);
  if (ev.ui != ui::UI_NONE || ev.action != ACTION_NONE) {
    lastInteractionMs = now;
    if (clockActive) {
      // in clock mode a touch only wakes the screen (doesn't run the action)
    } else if (ev.ui != ui::UI_NONE) {
      handleUi(ev.ui);
    } else {
      doAction(ev.action);
      if (ev.buttonIdx >= 0) renderer.flashButton(ev.buttonIdx);
    }
  }

  // --- sleep/wake sound: fires on the state transition ---
  const bool sleeping = pet.sleeping();
  if (sleeping && !wasSleeping) {
    audio.playSleepTune();
  } else if (!sleeping && wasSleeping) {
    audio.playWake();
  }
  wasSleeping = sleeping;

  // --- periodic persistence ---
  if (now - lastSaveMs > game::SAVE_INTERVAL_MS) {
    pet.save();
    lastSaveMs = now;
  }

  // --- web portal (serves requests; brings the server up on connect) ---
  web.handle();

  // --- ferret animation + render + LED ---
  ferret.update(pet, now);

  // mirror animation changes to the portal immediately (WS push)
  static uint32_t lastSeq = 0xFFFFFFFF;
  static bool lastFlip = false;
  if (ferret.animSeq() != lastSeq || ferret.faceLeft() != lastFlip) {
    lastSeq = ferret.animSeq();
    lastFlip = ferret.faceLeft();
    web.pushState();
  }

  String ip = web.connected() ? web.ip() : String();
  renderer.draw(pet, battery, ferret, menuOpen, audio.volume(), web.connected(),
                ip.c_str(), clockActive, petClock);
  led.update(pet.mood());

  delay(30);
}
