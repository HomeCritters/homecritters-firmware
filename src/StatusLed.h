#pragma once
#include <Adafruit_NeoPixel.h>
#include "Pet.h"
#include "pins.h"

// ============================================================
// StatusLed: mirrors the pet's mood on the RGB LED (WS2812).
// Brightness is adjustable (0..100, persisted). It can also run
// a game-over effect: 3 fast red blinks at full brightness, then
// solid red until the game is left (startDeath/endGame).
// ============================================================

class StatusLed {
 public:
  void begin();
  void update(Mood mood);        // mood-driven (call every loop)

  void setBrightness(int pct);   // 0..100, persisted to NVS
  int brightness() const { return _brightPct; }

  void startDeath();             // begin the game-over red effect
  void endGame();                // leaving the game -> back to mood

 private:
  Adafruit_NeoPixel _led{1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800};
  Mood _last = MOOD_HAPPY;
  bool _init = false;
  int _brightPct = 50;
  bool _death = false;
  unsigned long _deathStart = 0;

  void render(const uint8_t* c, uint8_t rawBright);
  void tickDeath();
};
