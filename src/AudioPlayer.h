#pragma once
#include <Arduino.h>
#include "audio/StreamRing.h"
#include "audio/AudioReader.h"

// ============================================================
// AudioPlayer: embedded SFX + streamed media through the ES8311
// codec + I2S.
//
// Media pipeline (Voice PE architecture):
//   AudioReader task (core 0, esp_http_client)
//     -> StreamRing (1MB PSRAM, SPSC)
//     -> decoder task (core 1, FLAC/WAV/MP3 by header sniff)
//     -> I2S DMA
// The big ring is the underrun defense; the reader keeps TCP
// drained independent of the I2S-paced decode.
//
// Only one sound plays at a time: a new play() preempts the
// current one; pet SFX are suppressed while media streams.
//
// ES8311 init is ported from Espressif's official sequence
// (esp-adf): slave mode, clock derived from SCLK/BCLK - follows
// the decoded sample rate (44.1/48kHz) automatically.
// ============================================================

class AudioPlayer {
 public:
  void begin();  // I2C + ES8311 + I2S + tasks

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
  void playThunder();    // storm lightning clap (skipped if audio is busy)
  void playRainAmb();    // ambient rain patter (weather scenes, if idle)
  void playWindAmb();    // ambient wind + creaky tree (fog/snow, if idle)
  void playListen();     // voice assistant: mic opened (ascending chime)
  void playConfirm();    // voice assistant: heard you, processing (descending)

  // --- media streaming (HA media player / Music Assistant / TTS) ---
  // Plays an http:// FLAC/MP3/WAV stream or file (no https). While a stream
  // is active, pet SFX are suppressed - stopStream() releases them.
  void playStream(const char* url);
  void stopStream();
  bool streaming() const { return _streaming; }

  // True while any audio plays (media stream OR a PROGMEM SFX). The mic
  // capture path is half-duplex with playback (shared I2S clock), so it must
  // suspend capture while this is true.
  bool busy() const { return _playing || _streaming; }

  // What is playing: HA TTS URLs go through /api/tts_proxy/, so speech is
  // distinguishable from music - the UI shows an Alexa-style ring for speech
  // and a party mode for music.
  enum MediaKind : uint8_t { MEDIA_NONE = 0, MEDIA_MUSIC, MEDIA_TTS };
  MediaKind mediaKind() const { return _streaming ? _kind : MEDIA_NONE; }
  // Live output loudness 0..255 (post-gain PCM envelope) - party LED beat.
  int mediaLevel() const;

  // Volume 0..100 (applied now, NVS via flushNvs; perceptual curve).
  void setVolume(int pct);
  int volume() const { return _volume; }
  void flushNvs();  // debounced persist - call every loop

  // Mic ADC gain step 0..7 (0/6/.../42 dB) - live tuning during bring-up.
  void setMicGain(int step);
  // Bring-up: read the mic for `ms` and print RMS/peak/DC (bypasses WS).
  void micSelfTest(int ms);

 private:
  void play(const unsigned char* data, unsigned int len);
  // Mixable SFX (WAV 16k mono): overlay on top of whatever plays (never over
  // TTS), decoder path when idle. One overlay channel - newest wins.
  void playMix(const unsigned char* wav, unsigned int len);
  void applyGain();

  int _volume = 80;
  unsigned long _nvsDirtyAt = 0;  // 0 = clean; else millis() of last change

  static void taskTrampoline(void* arg);
  void taskLoop();
  void startDecode(const unsigned char* data, unsigned int len);
  void startMedia(const char* url);  // decoder task only
  void stopMedia();                  // decoder task only: reader+decoder+ring
  void cleanupDecoder();             // decoder task only: _dec/_src

  // Request handoff main loop (core 1) -> decoder task. The spinlock keeps
  // {data, len, flag} consistent: without it the task could pick up a fresh
  // pointer with a stale length mid-update (true cross-core race).
  portMUX_TYPE _reqMux = portMUX_INITIALIZER_UNLOCKED;
  bool _startReq = false;
  const unsigned char* _reqData = nullptr;
  unsigned int _reqLen = 0;
  bool _streamReq = false;
  // volatile: RingSource polls this mid-read to abort waits responsively.
  volatile bool _stopReq = false;
  char _reqUrl[224] = {0};

  bool _playing = false;             // decoder-task only
  volatile bool _streaming = false;  // media active (read by main for state)
  volatile MediaKind _kind = MEDIA_NONE;  // set with _streaming in playStream()
  bool _live = false;                // decoder-task only: media (vs SFX) playing

  void* _dec = nullptr;  // AudioGenerator* (FLAC/WAV/MP3, opaque in the header)
  void* _src = nullptr;  // AudioFileSource* (PROGMEM for SFX, RingSource for media)
  void* _out = nullptr;  // AudioOutputI2S*

  StreamRing _ring;      // permanent 1MB PSRAM ring (media path)
  AudioReader _reader;   // core-0 network reader task
};
