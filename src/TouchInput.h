#pragma once
#include <stdint.h>

class LGFX_BallV2;

// ============================================================
// TouchInput: a single seam that merges the PHYSICAL touchscreen with a
// SYNTHETIC touch injected by the portal (the live-mirror canvas forwards
// pointer events as "touch:<phase>:<x>:<y>"). Every screen reads touch through
// touchinput::read() instead of lcd.getTouch() directly, so an injected point
// flows through the exact same tap/swipe/edge-pull detection as a real finger -
// no gesture logic is duplicated.
//
// inject() is called from the WS handler; read() from the loop. Both run on the
// same core-1 task (web.handle() runs _ws.loop() inside loop()), so there is no
// real cross-thread race - state is volatile only for consistency with the rest
// of the WS-input plumbing.
// ============================================================
namespace touchinput {

// Feed a synthetic touch point. phase down = pressed/moving, !down = released.
void inject(bool down, int32_t x, int32_t y);

// Physical touch wins; otherwise the injected point (auto-released if the
// stream went stale so a dropped connection can't wedge a finger down).
bool read(LGFX_BallV2& lcd, int32_t* x, int32_t* y);

}  // namespace touchinput
