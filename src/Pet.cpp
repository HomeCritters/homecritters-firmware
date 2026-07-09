#include "Pet.h"
#include "GameConfig.h"

using namespace game;

void Pet::begin() {
  _prefs.begin("tama", true);  // read-only
  _hunger  = _prefs.getFloat("hunger",  INITIAL_STAT);
  _energy  = _prefs.getFloat("energy",  INITIAL_STAT);
  _joy     = _prefs.getFloat("joy",     INITIAL_STAT);
  _hygiene = _prefs.getFloat("hygiene", INITIAL_STAT);
  _name    = _prefs.getString("name", "Furao");
  _prefs.end();
}

void Pet::save() {
  _prefs.begin("tama", false);
  _prefs.putFloat("hunger",  _hunger);
  _prefs.putFloat("energy",  _energy);
  _prefs.putFloat("joy",     _joy);
  _prefs.putFloat("hygiene", _hygiene);
  _prefs.putString("name", _name);
  _prefs.end();
}

void Pet::setName(const String& name) {
  _name = name;
  if (_name.length() == 0) _name = "Furao";
  if (_name.length() > 14) _name = _name.substring(0, 14);
  _prefs.begin("tama", false);
  _prefs.putString("name", _name);
  _prefs.end();
}

void Pet::update(float deltaMin) {
  if (deltaMin <= 0) return;

  const float factor = _sleeping ? SLEEP_DECAY_FACTOR : 1.0f;
  _hunger  = max(0.0f, _hunger  - deltaMin * HUNGER_DECAY_PER_MIN  * factor);
  _joy     = max(0.0f, _joy     - deltaMin * JOY_DECAY_PER_MIN     * factor);
  _hygiene = max(0.0f, _hygiene - deltaMin * HYGIENE_DECAY_PER_MIN * factor);

  if (_sleeping) {
    _energy = min(100.0f, _energy + deltaMin * ENERGY_RECOVER_PER_MIN);
  } else {
    _energy = max(0.0f, _energy - deltaMin * ENERGY_DECAY_PER_MIN);
  }
}

void Pet::apply(Action action) {
  switch (action) {
    case ACTION_FEED:  _hunger  = min(100.0f, _hunger  + FEED_GAIN);  break;
    case ACTION_PAT:   _joy     = min(100.0f, _joy     + PAT_GAIN);   break;
    case ACTION_CLEAN: _hygiene = min(100.0f, _hygiene + CLEAN_GAIN); break;
    case ACTION_TOGGLE_SLEEP: _sleeping = !_sleeping; break;
    case ACTION_NONE:  return;
  }
  save();
}

Mood Pet::mood() const {
  if (_sleeping) return MOOD_SLEEPY;
  if (_hunger  < HUNGRY_THRESHOLD) return MOOD_HUNGRY;
  if (_hygiene < DIRTY_THRESHOLD)  return MOOD_DIRTY;
  if (_energy  < SLEEPY_THRESHOLD) return MOOD_SLEEPY;

  const float avg = (_hunger + _energy + _joy + _hygiene) / 4.0f;
  if (avg > HAPPY_AVG)   return MOOD_HAPPY;
  if (avg > NEUTRAL_AVG) return MOOD_NEUTRAL;
  return MOOD_SAD;
}
