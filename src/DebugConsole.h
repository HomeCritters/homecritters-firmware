#pragma once
#include <Arduino.h>
#include <functional>

class Pet;
class Battery;
class AudioPlayer;
class StatusLed;
class Renderer;

// ============================================================
// DebugConsole: serial command console for development. Reads
// one command per line (see printHelp) and applies module-level
// commands (volume, LED, screen brightness, stats, battery)
// directly. Anything that touches main's screen state - "pet",
// "games", "menu:luz", "shot", actions - is forwarded to the
// navigate callback (main owns that state).
// Driven by tools/hwshot.py and tools/console.py.
// ============================================================

class DebugConsole {
 public:
  // navigate(cmd) returns true if it consumed the command.
  // onInteraction() is called for every non-passive command (so the idle
  // clock wakes, mirroring a real touch); passive ones (shot/help/bat)
  // skip it so a screenshot captures the screen exactly as-is.
  void begin(Pet* pet, Battery* battery, AudioPlayer* audio, StatusLed* led,
             Renderer* renderer,
             std::function<bool(const String&)> navigate,
             std::function<void()> onInteraction);
  void poll();  // call every loop

 private:
  void dispatch(const String& cmd);
  void printHelp();

  Pet* _pet = nullptr;
  Battery* _battery = nullptr;
  AudioPlayer* _audio = nullptr;
  StatusLed* _led = nullptr;
  Renderer* _renderer = nullptr;
  std::function<bool(const String&)> _navigate;
  std::function<void()> _onInteraction;
  String _line;
};
