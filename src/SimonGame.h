#pragma once
#include <Arduino.h>

// ============================================================
// SimonGame ("Genius"): the device plays a growing sequence of
// 4 colors (arc buttons light up + the RGB LED shows the color
// + one tone per color); the player repeats it by tapping the
// arcs (or the phone). One mistake (or a long hesitation) ends
// the run. Best score persists in NVS.
// Pure logic/timing - main drives sound/LED off litColor()
// transitions and the Renderer draws it.
// ============================================================

class SimonGame {
 public:
  enum State { SHOWING, WAITING, GAME_OVER };
  static constexpr int MAX_SEQ = 100;

  void begin();  // load the high score from NVS
  void reset();  // start a fresh run (1-step sequence)
  void update(unsigned long now);
  // Player pressed a color pad (0=green 1=red 2=yellow 3=blue).
  void press(int color, unsigned long now);

  State state() const { return _state; }
  bool gameOver() const { return _state == GAME_OVER; }
  int score() const { return _score; }  // completed rounds
  int hiScore() const { return _hiScore; }
  bool newRecord() const { return _newRecord; }
  // Currently lit color (0..3) or -1. Lights during playback AND on player
  // presses - main mirrors it to the LED and plays the tone on transitions.
  int litColor() const { return _lit; }

 private:
  void fail(unsigned long now);
  unsigned long onTimeMs() const;  // lit duration (speeds up as it grows)

  State _state = SHOWING;
  uint8_t _seq[MAX_SEQ];
  int _len = 1;
  int _showIdx = -1;   // item being shown (SHOWING)
  int _inputIdx = 0;   // next expected item (WAITING)
  int _lit = -1;
  int _score = 0;
  int _hiScore = 0;
  bool _newRecord = false;
  unsigned long _nextEvent = 0;  // SHOWING timeline
  unsigned long _litUntil = 0;   // press feedback (WAITING)
  unsigned long _deadline = 0;   // input timeout (WAITING)
};
