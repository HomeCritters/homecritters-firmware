#include "DebugConsole.h"
#include "Pet.h"
#include "Battery.h"
#include "AudioPlayer.h"
#include "StatusLed.h"
#include "Renderer.h"

void DebugConsole::begin(Pet* pet, Battery* battery, AudioPlayer* audio, StatusLed* led,
                         Renderer* renderer,
                         std::function<bool(const String&)> navigate,
                         std::function<void()> onInteraction) {
  _pet = pet;
  _battery = battery;
  _audio = audio;
  _led = led;
  _renderer = renderer;
  _navigate = navigate;
  _onInteraction = onInteraction;
}

void DebugConsole::printHelp() {
  Serial.println(F("[console] commands:"));
  Serial.println(F("  shot                 - screenshot the current screen"));
  Serial.println(F("  pet | games | doodle | ball"));
  Serial.println(F("  menu[:main|audio|luz|qr]"));
  Serial.println(F("  feed | pat | clean | sleep"));
  Serial.println(F("  vol:N | led:N | scr:N        (0..100)"));
  Serial.println(F("  stats:H,E,J,Hy               (0..100 each)"));
  Serial.println(F("  bat                  - battery voltage + percent"));
}

void DebugConsole::dispatch(const String& c) {
  // Passive commands: no interaction stamp, so a screenshot captures the
  // screen exactly as-is (idle clock included) instead of waking it.
  if (c == "help" || c == "?") { printHelp(); return; }
  if (c == "bat") {
    Serial.printf("[bat] %.3fV -> %d%%\n", _battery->voltage(), _battery->percent());
    return;
  }
  if (c == "shot") { _navigate(c); return; }

  if (_onInteraction) _onInteraction();  // wakes the idle clock, like a touch

  if (c.startsWith("vol:")) { _audio->setVolume(c.substring(4).toInt()); return; }
  if (c.startsWith("led:")) { _led->setBrightness(c.substring(4).toInt()); return; }
  if (c.startsWith("scr:")) { _renderer->setScreenBrightness(c.substring(4).toInt()); return; }

  if (c.startsWith("stats:")) {  // "stats:H,E,J,Hy"
    const String v = c.substring(6);
    int a = v.indexOf(','), b = v.indexOf(',', a + 1), d = v.indexOf(',', b + 1);
    if (a > 0 && b > 0 && d > 0) {
      _pet->debugSetStats(v.substring(0, a).toFloat(), v.substring(a + 1, b).toFloat(),
                          v.substring(b + 1, d).toFloat(), v.substring(d + 1).toFloat());
    }
    return;
  }

  if (_navigate && _navigate(c)) return;  // screen navigation / pet actions
  Serial.printf("[console] unknown: %s (try 'help')\n", c.c_str());
}

void DebugConsole::poll() {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      _line.trim();
      if (_line.length()) dispatch(_line);
      _line = "";
    } else if (_line.length() < 64) {
      _line += ch;
    }
  }
}
