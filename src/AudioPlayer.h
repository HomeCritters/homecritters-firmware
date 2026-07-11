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

  void stop();
  bool isPlaying() const { return _playing; }

  // Volume 0..100 (persisted to NVS, perceptual curve).
  void setVolume(int pct);
  int volume() const { return _volume; }

 private:
  void play(const unsigned char* data, unsigned int len);
  void applyGain();

  int _volume = 80;

  static void taskTrampoline(void* arg);
  void taskLoop();
  void startDecode();
  void cleanup();

  volatile bool _startReq = false;
  volatile bool _stopReq  = false;
  volatile bool _playing  = false;
  const unsigned char* volatile _reqData = nullptr;
  volatile unsigned int _reqLen = 0;

  void* _mp3 = nullptr;  // AudioGeneratorMP3*      (opaque in the header)
  void* _src = nullptr;  // AudioFileSourcePROGMEM*
  void* _out = nullptr;  // AudioOutputI2S*
};
