#include "StatusLed.h"
#include "Theme.h"

void StatusLed::begin() {
  _led.begin();
  _led.setBrightness(80);
  _init = false;
}

void StatusLed::update(Mood mood) {
  // Only rewrite the LED when the mood changes (avoids I/O every frame).
  if (_init && mood == _last) return;
  _last = mood;
  _init = true;

  const uint8_t* c;
  switch (mood) {
    case MOOD_HAPPY:   c = theme::led::HAPPY;   break;
    case MOOD_NEUTRAL: c = theme::led::NEUTRAL; break;
    case MOOD_SAD:     c = theme::led::SAD;     break;
    case MOOD_HUNGRY:  c = theme::led::HUNGRY;  break;
    case MOOD_SLEEPY:  c = theme::led::SLEEPY;  break;
    case MOOD_DIRTY:   c = theme::led::DIRTY;   break;
    default:           c = theme::led::NEUTRAL; break;
  }
  _led.setPixelColor(0, _led.Color(c[0], c[1], c[2]));
  _led.show();
}
