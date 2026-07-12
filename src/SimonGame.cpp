#include "SimonGame.h"
#include <Preferences.h>
#include <esp_random.h>

static constexpr unsigned long START_DELAY_MS  = 800;  // breath before showing
static constexpr unsigned long GAP_MS          = 170;  // dark gap between items
static constexpr unsigned long ROUND_PAUSE_MS  = 700;  // pause after a round
static constexpr unsigned long PRESS_LIT_MS    = 280;  // player press feedback
static constexpr unsigned long INPUT_TIMEOUT_MS = 6000; // hesitation = game over
static constexpr unsigned long ON_BASE_MS = 420, ON_MIN_MS = 240;

void SimonGame::begin() {
  Preferences p;
  p.begin("game", true);
  _hiScore = p.getInt("shs", 0);
  p.end();
}

// The sequence plays faster as it grows (classic Genius pressure).
unsigned long SimonGame::onTimeMs() const {
  const long t = (long)ON_BASE_MS - (long)_len * 10;
  return t < (long)ON_MIN_MS ? ON_MIN_MS : (unsigned long)t;
}

void SimonGame::reset() {
  _state = SHOWING;
  _len = 1;
  _seq[0] = esp_random() % 4;
  _showIdx = -1;
  _inputIdx = 0;
  _lit = -1;
  _score = 0;
  _newRecord = false;
  _nextEvent = millis() + START_DELAY_MS;
}

void SimonGame::update(unsigned long now) {
  if (_state == SHOWING) {
    if (now < _nextEvent) return;
    if (_lit >= 0) {  // finished lighting an item
      _lit = -1;
      if (_showIdx + 1 >= _len) {  // whole sequence shown -> player's turn
        _state = WAITING;
        _inputIdx = 0;
        _deadline = now + INPUT_TIMEOUT_MS;
      } else {
        _nextEvent = now + GAP_MS;
      }
    } else {  // gap over -> light the next item
      _showIdx++;
      _lit = _seq[_showIdx];
      _nextEvent = now + onTimeMs();
    }
  } else if (_state == WAITING) {
    if (_lit >= 0 && now >= _litUntil) _lit = -1;
    if (now > _deadline) fail(now);  // took too long
  }
}

void SimonGame::press(int color, unsigned long now) {
  if (_state != WAITING || color < 0 || color > 3) return;
  _lit = color;  // press feedback: main mirrors it to the LED + tone
  _litUntil = now + PRESS_LIT_MS;
  _deadline = now + INPUT_TIMEOUT_MS;

  if (color != _seq[_inputIdx]) { fail(now); return; }

  if (++_inputIdx >= _len) {  // round complete: extend and replay
    _score++;
    if (_len < MAX_SEQ) _seq[_len++] = esp_random() % 4;
    _state = SHOWING;
    _showIdx = -1;
    _nextEvent = now + ROUND_PAUSE_MS;  // the last press stays lit as "right!"
  }
}

void SimonGame::fail(unsigned long now) {
  (void)now;
  _state = GAME_OVER;
  _lit = -1;
  if (_score > _hiScore) {  // persist the new record right away
    _hiScore = _score;
    _newRecord = true;
    Preferences p;
    p.begin("game", false);
    p.putInt("shs", _hiScore);
    p.end();
  }
}
