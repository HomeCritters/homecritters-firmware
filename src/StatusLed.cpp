#include "StatusLed.h"
#include <Preferences.h>
#include "Theme.h"

static const uint8_t RED[3] = {255, 0, 0};
static const uint8_t OFF[3] = {0, 0, 0};

// Percent (0..100) -> NeoPixel raw brightness (0..255).
static uint8_t pctToRaw(int pct) { return (uint8_t)((constrain(pct, 0, 100) * 255 + 50) / 100); }

static const uint8_t* moodColor(Mood mood) {
  switch (mood) {
    case MOOD_HAPPY:   return theme::led::HAPPY;
    case MOOD_NEUTRAL: return theme::led::NEUTRAL;
    case MOOD_SAD:     return theme::led::SAD;
    case MOOD_HUNGRY:  return theme::led::HUNGRY;
    case MOOD_SLEEPY:  return theme::led::SLEEPY;
    case MOOD_DIRTY:   return theme::led::DIRTY;
    default:           return theme::led::NEUTRAL;
  }
}

void StatusLed::begin() {
  _led.begin();
  Preferences p;
  p.begin("led", true);
  _brightPct = p.getInt("bright", 50);
  p.end();
  _init = false;
}

// Set brightness first, then the (unscaled) color, then show: this writes a
// freshly-scaled pixel and avoids the cumulative rounding of repeated
// setBrightness() calls on an already-scaled buffer.
void StatusLed::render(const uint8_t* c, uint8_t rawBright) {
  _led.setBrightness(rawBright);
  _led.setPixelColor(0, _led.Color(c[0], c[1], c[2]));
  _led.show();
}

void StatusLed::update(Mood mood) {
  if (_death) { tickDeath(); return; }
  if (_override) return;  // a game owns the LED right now
  // Only rewrite the LED when the mood changes (avoids I/O every frame).
  if (_init && mood == _last) return;
  _last = mood;
  _init = true;
  render(moodColor(mood), pctToRaw(_brightPct));
}

void StatusLed::setBrightness(int pct) {
  _brightPct = constrain(pct, 0, 100);
  Preferences p;
  p.begin("led", false);
  p.putInt("bright", _brightPct);
  p.end();
  if (!_death) render(moodColor(_last), pctToRaw(_brightPct));  // apply now
}

void StatusLed::startDeath() {
  _death = true;
  _deathStart = millis();
  _deathShown = false;
}

void StatusLed::gameColor(uint8_t r, uint8_t g, uint8_t b) {
  _override = true;
  const uint8_t c[3] = {r, g, b};
  render(c, 255);  // full brightness: the LED is the game display
}

void StatusLed::gameOff() {
  _override = true;
  render(OFF, 255);
}

void StatusLed::endGame() {
  if (!_death && !_override) return;
  _death = false;
  _override = false;
  _init = false;  // force a mood re-render on the next update()
}

// 3 fast blinks (on 90ms / off 90ms) at full brightness, then solid red until
// endGame(). Driven by update() each loop; only writes the LED when the blink
// phase actually changes (a WS2812 show() per frame is wasted blocking I/O).
void StatusLed::tickDeath() {
  static constexpr unsigned long BLINK = 90;
  const unsigned long t = millis() - _deathStart;
  const bool on = (t >= 6 * BLINK) || ((t / BLINK) % 2 == 0);  // 3 on-phases, then solid
  if (_deathShown && on == _deathOn) return;
  _deathOn = on;
  _deathShown = true;
  render(on ? RED : OFF, 255);  // max brightness for the death effect
}
