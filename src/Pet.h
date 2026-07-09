#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ============================================================
// Pet: the pet's "model". Owns the 4 stats, time-based decay,
// actions and NVS persistence. Knows nothing about the screen,
// LED or audio (other modules handle those) - game rules only.
// ============================================================

enum Mood {
  MOOD_HAPPY,
  MOOD_NEUTRAL,
  MOOD_SAD,
  MOOD_HUNGRY,
  MOOD_SLEEPY,
  MOOD_DIRTY,
};

// Actions the outside world can trigger on the pet.
enum Action {
  ACTION_NONE,
  ACTION_FEED,
  ACTION_PAT,
  ACTION_CLEAN,
  ACTION_TOGGLE_SLEEP,
};

class Pet {
 public:
  void begin();                 // load saved state
  void update(float deltaMin);  // apply decay for the elapsed time
  void apply(Action action);    // run an action (and save)
  void save();                  // persist to NVS

  Mood mood() const;
  bool sleeping() const { return _sleeping; }

  const String& name() const { return _name; }
  void setName(const String& name);  // update and persist

  float hunger()  const { return _hunger; }
  float energy()  const { return _energy; }
  float joy()     const { return _joy; }
  float hygiene() const { return _hygiene; }

 private:
  float _hunger  = 0;
  float _energy  = 0;
  float _joy     = 0;
  float _hygiene = 0;
  bool  _sleeping = false;
  String _name = "Furao";

  Preferences _prefs;
};
