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

  void setBrightness(int pct);   // 0..100 (applied now, NVS via flushNvs)
  int brightness() const { return _brightPct; }
  void flushNvs();               // debounced persist - call every loop

  void startDeath();             // begin the game-over red effect
  void endGame();                // leaving the game -> back to mood

  // Game color override (Genius): show a solid color / go dark at max
  // brightness until endGame() releases the LED back to the mood.
  void gameColor(uint8_t r, uint8_t g, uint8_t b);
  void gameOff();

 private:
  Adafruit_NeoPixel _led{1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800};
  Mood _last = MOOD_HAPPY;
  bool _init = false;
  int _brightPct = 50;
  unsigned long _nvsDirtyAt = 0;  // 0 = clean; else millis() of last change
  bool _death = false;
  bool _override = false;    // game color override active (Genius)
  bool _deathOn = false;     // current blink phase (avoid redundant writes)
  bool _deathShown = false;  // has the current phase been rendered yet?
  unsigned long _deathStart = 0;

  void render(const uint8_t* c, uint8_t rawBright);
  void tickDeath();
};
