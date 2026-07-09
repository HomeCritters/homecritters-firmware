#pragma once
#include <Adafruit_NeoPixel.h>
#include "Pet.h"
#include "pins.h"

// ============================================================
// StatusLed: mirrors the pet's mood on the RGB LED (WS2812).
// ============================================================

class StatusLed {
 public:
  void begin();
  void update(Mood mood);

 private:
  Adafruit_NeoPixel _led{1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800};
  Mood _last = MOOD_HAPPY;
  bool _init = false;
};
