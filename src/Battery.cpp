#include "Battery.h"
#include <Arduino.h>
#include "pins.h"

static constexpr unsigned long SAMPLE_INTERVAL_MS = 5000;

void Battery::begin() {
  analogSetAttenuation(ADC_11db);
}

// analogReadMilliVolts applies the chip's factory ADC calibration, which
// corrects the ESP32 ADC's nonlinearity (raw*Vref/4095 reads several % low,
// so a full 4.2V pack looked like ~90%). Average a few samples for stability.
// Sampling is throttled: the render loop reads percent() every frame while
// the config menu is open, and 16 ADC conversions per frame is real I/O.
void Battery::refresh() const {
  const unsigned long now = millis();
  if (_sampledAt != 0 && now - _sampledAt < SAMPLE_INTERVAL_MS) return;
  _sampledAt = now;

  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BATTERY_ADC);
  mv /= 16;
  _volts = (mv / 1000.0f) * 2.0f;  // undo the on-board /2 divider
}

float Battery::voltage() const {
  refresh();
  return _volts;
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
