#include "TouchInput.h"
#include <Arduino.h>
#include "LGFX_BallV2.h"

namespace touchinput {

static volatile bool s_down = false;      // synthetic finger currently pressed
static volatile bool s_pendingUp = false; // release deferred until read once
static volatile int32_t s_x = 0, s_y = 0;
static volatile unsigned long s_stamp = 0;  // last update (staleness guard)

void inject(bool down, int32_t x, int32_t y) {
  s_x = x;
  s_y = y;
  s_stamp = millis();
  if (down) {
    s_down = true;
    s_pendingUp = false;
  } else {
    // Defer the release to the next read() so a tap whose down+up land in the
    // same render frame (e.g. a stall during heavy audio) is still seen once.
    s_pendingUp = true;
  }
}

bool read(LGFX_BallV2& lcd, int32_t* x, int32_t* y) {
  if (lcd.getTouch(x, y)) return true;  // a real finger always wins
  if (s_down && millis() - s_stamp < 1500) {
    *x = s_x;
    *y = s_y;
    if (s_pendingUp) {  // this read gets the final point; the next one releases
      s_down = false;
      s_pendingUp = false;
    }
    return true;
  }
  s_down = false;  // stale: drop the held point
  return false;
}

}  // namespace touchinput
