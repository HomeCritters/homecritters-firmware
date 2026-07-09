#include "Battery.h"
#include <Arduino.h>
#include "pins.h"

void Battery::begin() {
  analogSetAttenuation(ADC_11db);
}

float Battery::voltage() const {
  // The board's divider makes the ADC read ~half the battery voltage.
  int raw = analogRead(PIN_BATTERY_ADC);
  return (raw / 4095.0f) * 3.3f * 2.0f;
}

int Battery::percent() const {
  const float v = voltage();
  struct Pt { float v; int p; };
  static const Pt table[] = {
    {2.80f, 0}, {3.30f, 20}, {3.60f, 40}, {3.70f, 50},
    {3.80f, 70}, {4.00f, 90}, {4.20f, 100},
  };
  constexpr int N = sizeof(table) / sizeof(table[0]);

  if (v <= table[0].v) return 0;
  for (int i = 1; i < N; i++) {
    if (v <= table[i].v) {
      float t = (v - table[i - 1].v) / (table[i].v - table[i - 1].v);
      return (int)(table[i - 1].p + t * (table[i].p - table[i - 1].p));
    }
  }
  return 100;
}
