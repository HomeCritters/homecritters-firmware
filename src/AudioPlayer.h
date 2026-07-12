#pragma once
#include <Arduino.h>

// ============================================================
// AudioPlayer: plays embedded MP3s (sleep tune + sound effects)
// through the ES8311 codec + I2S. Decoding runs on a dedicated
// task (core 0) so it never stalls the render/UI loop (core 1).
//
// Only one sound plays at a time: a new play() preempts the
// current one.
//
// ES8311 init is ported from Espressif's official sequence
// (esp-adf): slave mode, clock derived from SCLK/BCLK. SCLK mode
// follows the MP3 sample rate (44.1/48kHz) automatically.
// ============================================================

class AudioPlayer {
 public:
  void begin();  // I2C + ES8311 + I2S + audio task

  // Effects / music (non-blocking; preempts whatever is playing).
  void playSleepTune();  // snoring loop for sleep
  void playWake();       // wake up
  void playEat();        // feeding
  void playDrink();      // bath / water
  void playPat();        // petting
  void playJump();       // doodle-jump bounce
  void playBoost();      // doodle-jump spring boost
  void playCrumble();    // doodle-jump dirt platform breaking
  void playRecord();     // game over with a new high score
  void playDeath();      // game over
  void playThrow();      // bolinha throw (whoosh)
  void playCamera();     // screenshot shutter
  void playClick();      // UI button/tab click
  void playSimon(int color);  // Genius tone (0=green 1=red 2=yellow 3=blue)
  void playBuzzer();     // Genius wrong answer

  // Volume 0..100 (persisted to NVS, perceptual curve).
  void setVolume(int pct);
  int volume() const { return _volume; }

 private:
  void play(const unsigned char* data, unsigned int len);
  void applyGain();

  int _volume = 80;

  static void taskTrampoline(void* arg);
  void taskLoop();
  void startDecode(const unsigned char* data, unsigned int len);
  void cleanup();

  // Request handoff main loop (core 1) -> audio task (core 0). The spinlock
  // keeps {data, len, flag} consistent: without it the task could pick up a
  // fresh pointer with a stale length mid-update (true cross-core race).
  portMUX_TYPE _reqMux = portMUX_INITIALIZER_UNLOCKED;
  bool _startReq = false;
  const unsigned char* _reqData = nullptr;
  unsigned int _reqLen = 0;

  bool _playing = false;  // audio-task only

  void* _dec = nullptr;  // AudioGenerator* (MP3 or WAV, opaque in the header)
  void* _src = nullptr;  // AudioFileSourcePROGMEM*
  void* _out = nullptr;  // AudioOutputI2S*
};
