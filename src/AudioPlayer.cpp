#include "AudioPlayer.h"
#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioGeneratorFLAC.h>
#include <AudioFileSourcePROGMEM.h>
#include "audio/AudioCodec.h"
#include "audio/StreamRing.h"
#include "audio/RingSource.h"
#include "pins.h"
#include "sounds/sleep_music.h"
#include "sounds/sfx_eat.h"
#include "sounds/sfx_drink.h"
#include "sounds/sfx_tap.h"
#include "sounds/sfx_wake.h"
#include "sounds/sfx_jump.h"
#include "sounds/sfx_boost.h"
#include "sounds/sfx_crumble.h"
#include "sounds/sfx_record.h"
#include "sounds/sfx_death.h"
#include "sounds/sfx_throw.h"
#include "sounds/sfx_camera.h"
#include "sounds/sfx_click.h"
#include "sounds/simon_green.h"
#include "sounds/simon_red.h"
#include "sounds/simon_yellow.h"
#include "sounds/simon_blue.h"
#include "sounds/sfx_buzzer.h"
#include "sounds/sfx_listen.h"
#include "sounds/sfx_confirm.h"

// ============================================================
// Minimal ES8311 driver (playback/DAC only).
// Sequence ported from espressif/esp-adf .../es8311/es8311.c
// Fixed config for this board: slave mode, clock from SCLK,
// 48kHz, 16-bit, standard I2S format, DAC output (mono - left
// I2S channel).
// ============================================================
namespace {

uint8_t es8311_addr = 0x18;  // 0x18 (CE=0) or 0x19 (CE=1)

void esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(es8311_addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool esProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Each {reg, val} pair carries the final value precomputed for the fixed
// config above (avoids read-modify-write; power-on defaults are 0).
void es8311_init() {
  // Detect the I2C address (some boards use 0x19).
  if (!esProbe(0x18) && esProbe(0x19)) es8311_addr = 0x19;

  static const uint8_t seq[][2] = {
    {0x44, 0x08}, {0x44, 0x08},  // I2C noise immunity on the 1st write
    {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24},
    {0x04, 0x10}, {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00},
    {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80},  // power on CSM
    {0x00, 0x80},                              // slave mode (bit6=0)
    {0x01, 0x3F}, {0x01, 0xBF},                // clock from SCLK (bit7=1)
    // config_sample(48kHz) in SCLK mode (coeff 12.288MHz / 48kHz):
    {0x02, 0x18}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x10},
    {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03},
    {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A},
    // 16-bit + standard I2S format:
    {0x09, 0x0C}, {0x0A, 0x0C},
    // start in DAC mode:
    {0x09, 0x0C}, {0x0A, 0x4C}, {0x17, 0xBF}, {0x0E, 0x02},
    {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
    {0x37, 0x08}, {0x45, 0x00}, {0x44, 0x58},
    {0x31, 0x00},  // DAC unmuted
    {0x32, 0xE0},  // DAC volume (~loud; 0xBF=0dB, 0xFF=+32dB)
    // Enable the ADC (mic) path for full-duplex. Mirrors esp-adf
    // es8311_start(BOTH): the key is SDPOUT reg 0x0A bit6 CLEAR (the DAC-only
    // config left it 0x4C = ADC serial output muted); 0x0C = 16-bit I2S with
    // the ADC output enabled. 0x0E/0x14/0x0D/0x15/0x17 (power/PGA/ADC) are
    // already written above. No DAC->ADC loopback (0x44 bit7 = 0).
    {0x0A, 0x0C},  // SDPOUT: ADC serial output on, 16-bit I2S
    {0x16, 0x04},  // ADC/mic gain: 0..7 = 0/6/12/18/24/30/36/42 dB (24dB start)
  };
  for (auto& s : seq) esWrite(s[0], s[1]);
}

// Live mic PGA gain tuning (reg 0x16), 0..7 = 0..42dB. Called from the debug
// console to calibrate the capture level without reflashing.
void es8311_set_mic_gain(uint8_t step) { esWrite(0x16, step & 0x07); }

}  // namespace

// Convenience casts for the opaque pointers stored in the header.
#define DEC (static_cast<AudioGenerator*>(_dec))
#define SRC (static_cast<AudioFileSource*>(_src))
#define OUT (static_cast<CodecOutput*>(_out))

// Pick the decoder from the leading header bytes, matching what Music Assistant
// (and ESPHome speakers) send: FLAC for lossless media - cheap for the server
// to encode (unlike realtime 320k MP3) and half the bandwidth of WAV; WAV/PCM
// for announcements; MP3 for TTS proxies and radio. Returns the AudioGenerator*
// (as void* to match _dec) and sets *name for logging.
static void* makeDecoder(const uint8_t* h, uint32_t n, const char** name) {
  if (n >= 4 && memcmp(h, "fLaC", 4) == 0) {
    *name = "flac";
    return new AudioGeneratorFLAC();
  }
  if (n >= 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
    *name = "wav";
    return new AudioGeneratorWAV();
  }
  *name = "mp3";
  return new AudioGeneratorMP3();
}

// Media ring buffer (PSRAM, permanent): the Voice PE anti-underrun defense.
// 1MB is ~8s of 48kHz stereo FLAC, ~60s of 128k MP3 - the reader front-runs
// the decoder and absorbs any server pacing/WiFi jitter.
static constexpr uint32_t RING_BYTES = 1000u * 1000u;

// How much to buffer BEFORE starting the decoder. An empty start chopped the
// first second of TTS. Smaller than the old 96KB: the reader task now keeps
// filling independently of the decode, so this only covers the spin-up.
static constexpr uint32_t PREFILL_BYTES = 64u * 1024;

void AudioPlayer::begin() {
  // I2C bus "A" dedicated to the codec (the touch uses bus 1).
  Wire.begin(PIN_I2C_A_SDA, PIN_I2C_A_SCL, 400000);
  es8311_init();

  // Enable the speaker amplifier.
  pinMode(PIN_SPEAKER_EN, OUTPUT);
  digitalWrite(PIN_SPEAKER_EN, HIGH);

  // Full-duplex I2S (port 0): AudioCodec owns the driver (playback TX + mic RX
  // on the shared clock), replacing ESP8266Audio's TX-only AudioOutputI2S.
  // MCLK stays on GPIO16 - the ~11MHz clock on GPIO0 (BOOT pad) desensed WiFi
  // whenever audio played (RTT 21->224ms, throughput 273->20 KB/s). CodecOutput
  // is the sink the decoders write into.
  if (!AudioCodec::begin()) Serial.println("[audio] I2S install failed");
  _out = new CodecOutput();

  // Saved volume (default 80%).
  Preferences p;
  p.begin("audio", true);
  _volume = p.getInt("vol", 80);
  p.end();
  applyGain();

  // Two-core split, mirroring Voice PE's reader/decoder pipeline:
  //   - READER on core 0 (PRO, with WiFi/lwIP): AudioReader task, pure I/O,
  //     drains TCP into the permanent PSRAM ring (created in _reader.begin).
  //   - DECODER here on core 1 (APP): heavy codecs (FLAC especially) are
  //     CPU-bound; on core 0 they starved the WiFi stack. Core 1 shares with
  //     the render loop, but the scene render is frozen while audio streams
  //     (see main.cpp). Priority 5 (render loop runs at 1): the decoder
  //     blocks on I2S DMA writes most of the time, so this can't starve the
  //     UI, but guarantees the DMA never starves when core 1 is busy - the
  //     intent of Voice PE's high-priority speaker task.
  // Busy tasks can starve the idle tasks, so unwatch both WDTs.
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

  if (!_ring.alloc(RING_BYTES)) Serial.println("[audio] ring alloc failed");
  _reader.begin(&_ring);
  xTaskCreatePinnedToCore(taskTrampoline, "audio", 12288, this, 5, nullptr, 1);
}

void AudioPlayer::applyGain() {
  if (!_out) return;
  // Perceptual (exponential) curve: hearing is logarithmic, so a linear
  // gain sounds loud at the low end. Raising to ~2.5 makes low levels
  // genuinely quiet (10% -> ~0.3% amplitude) while keeping 100% full.
  float v = _volume / 100.0f;
  OUT->SetGain(powf(v, 2.5f));
}

void AudioPlayer::setVolume(int pct) {
  _volume = constrain(pct, 0, 100);
  applyGain();
  Preferences p;
  p.begin("audio", false);
  p.putInt("vol", _volume);
  p.end();
}

void AudioPlayer::setMicGain(int step) { es8311_set_mic_gain((uint8_t)constrain(step, 0, 7)); }

// Bring-up self-test: read the mic directly for `ms` and print level stats
// (RMS / peak / DC). Bypasses the WebSocket entirely. Assumes no playback.
void AudioPlayer::micSelfTest(int ms) {
  if (busy()) { Serial.println("[mic] busy (audio playing)"); return; }
  AudioCodec::setCaptureRate();
  int16_t buf[320];
  int64_t sum = 0, sumsq = 0;
  int n = 0, peak = 0;
  const uint32_t t0 = millis();
  while ((int)(millis() - t0) < ms) {
    const size_t got = AudioCodec::readMicMono(buf, 320, 50);
    for (size_t i = 0; i < got; i++) {
      const int v = buf[i];
      sum += v;
      sumsq += (int64_t)v * v;
      if (abs(v) > peak) peak = abs(v);
      n++;
    }
  }
  if (!n) { Serial.println("[mic] no samples"); return; }
  const double dc = (double)sum / n;
  const double rms = sqrt((double)sumsq / n - dc * dc);
  Serial.printf("[mic] %dms n=%d RMS=%.0f peak=%d (%.0f%%FS) DC=%.0f\n", ms, n, rms,
                peak, 100.0 * peak / 32767.0, dc);
}

// Request a clip (preempts the current one). Non-blocking. Suppressed while a
// media stream plays - a pet SFX must not kill the music (single decoder).
void AudioPlayer::play(const unsigned char* data, unsigned int len) {
  if (_streaming) return;
  portENTER_CRITICAL(&_reqMux);
  _reqData = data;
  _reqLen = len;
  _startReq = true;
  portEXIT_CRITICAL(&_reqMux);
}

// Request an http:// MP3 stream (HA media player). Non-blocking.
void AudioPlayer::playStream(const char* url) {
  if (!url || strncmp(url, "http://", 7) != 0) {
    Serial.println("[media] only http:// URLs are supported");
    return;
  }
  portENTER_CRITICAL(&_reqMux);
  strlcpy(_reqUrl, url, sizeof(_reqUrl));
  _streamReq = true;
  _startReq = false;  // a pending SFX loses to the stream
  portEXIT_CRITICAL(&_reqMux);
  // Speech vs music: HA's TTS always serves via /api/tts_proxy/.
  _kind = strstr(url, "tts_proxy") ? MEDIA_TTS : MEDIA_MUSIC;
  // Report "playing" right away (open+prefill take 1-2s). Music Assistant
  // watches the player state after sending play; seeing "idle" that long
  // makes it give up on the session. startMedia() clears it on failure.
  _streaming = true;
}

void AudioPlayer::stopStream() {
  portENTER_CRITICAL(&_reqMux);
  _stopReq = true;
  _streamReq = false;
  portEXIT_CRITICAL(&_reqMux);
}

void AudioPlayer::playSleepTune() { play(sleep_music_mp3, sleep_music_mp3_len); }
void AudioPlayer::playWake()      { play(sfx_wake_mp3,     sfx_wake_mp3_len); }
void AudioPlayer::playEat()       { play(sfx_eat_mp3,      sfx_eat_mp3_len); }
void AudioPlayer::playDrink()     { play(sfx_drink_mp3,    sfx_drink_mp3_len); }
void AudioPlayer::playPat()       { play(sfx_tap_mp3,      sfx_tap_mp3_len); }
void AudioPlayer::playJump()      { play(sfx_jump_mp3,     sfx_jump_mp3_len); }
void AudioPlayer::playBoost()     { play(sfx_boost_mp3,    sfx_boost_mp3_len); }
void AudioPlayer::playCrumble()   { play(sfx_crumble_mp3,  sfx_crumble_mp3_len); }
void AudioPlayer::playRecord()    { play(sfx_record_mp3,   sfx_record_mp3_len); }
void AudioPlayer::playDeath()     { play(sfx_death_mp3,    sfx_death_mp3_len); }
void AudioPlayer::playThrow()     { play(sfx_throw_mp3,    sfx_throw_mp3_len); }
void AudioPlayer::playCamera()    { play(sfx_camera_mp3,   sfx_camera_mp3_len); }
void AudioPlayer::playClick()     { play(sfx_click_mp3,    sfx_click_mp3_len); }
void AudioPlayer::playBuzzer()    { play(sfx_buzzer_mp3,   sfx_buzzer_mp3_len); }
void AudioPlayer::playListen()    { play(sfx_listen_mp3,   sfx_listen_mp3_len); }
void AudioPlayer::playConfirm()   { play(sfx_confirm_wav,  sfx_confirm_wav_len); }

void AudioPlayer::playSimon(int color) {
  switch (color) {
    case 0: play(simon_green_wav,  simon_green_wav_len);  break;
    case 1: play(simon_red_wav,    simon_red_wav_len);    break;
    case 2: play(simon_yellow_wav, simon_yellow_wav_len); break;
    case 3: play(simon_blue_wav,   simon_blue_wav_len);   break;
  }
}

void AudioPlayer::startDecode(const unsigned char* data, unsigned int len) {
  cleanupDecoder();
  if (!data || len == 0) return;
  _src = new AudioFileSourcePROGMEM(data, len);
  // Pick the decoder by the magic bytes: "RIFF" = WAV (the generated Genius
  // tones), anything else = MP3.
  const bool wav = len > 4 && data[0] == 'R' && data[1] == 'I' &&
                   data[2] == 'F' && data[3] == 'F';
  _dec = wav ? (void*)new AudioGeneratorWAV() : (void*)new AudioGeneratorMP3();
  DEC->begin(SRC, OUT);
  _playing = true;
}

// Start media playback: reader task (core 0) -> ring -> decoder (this task).
// Runs on the decoder task only.
// NOTE: _streaming is already true (set optimistically by playStream() so the
// reported state flips to "playing" instantly - Music Assistant gives up if
// the player stays "idle" after a play command). Only clear it on failure.
void AudioPlayer::startMedia(const char* url) {
  stopMedia();  // preempt whatever is playing (SFX or previous media)

  if (!_reader.start(url)) {  // validates http:// and length
    Serial.printf("[media] reader start failed: %s\n", url);
    _streaming = false;
    return;
  }

  // Prefill before starting the decoder: an empty start chopped the first
  // second of TTS. Bail early when the fill plateaus (small clip hit EOF or
  // the server paces slowly - start with what we have) or on the deadline.
  const uint32_t t0 = millis();
  uint32_t lastLvl = 0, lastRise = t0;
  for (;;) {
    if (_stopReq || _streamReq) return;  // superseded: taskLoop handles it next
    const uint32_t lvl = _ring.fill();
    if (lvl >= PREFILL_BYTES || _ring.eof()) break;
    if (lvl != lastLvl) {
      lastLvl = lvl;
      lastRise = millis();
    } else if (millis() - lastRise > 1500) {
      break;  // truly stalled: go with what we have
    }
    if (millis() - t0 > 5000) break;  // hard cap on startup delay
    vTaskDelay(1);
  }
  if (_ring.fill() == 0) {  // nothing arrived at all (bad URL/server)
    Serial.println("[media] no data");
    stopMedia();
    _streaming = false;
    return;
  }

  // Sniff the buffered header to pick the decoder. FLAC ("fLaC") is what
  // Music Assistant sends for lossless media (the format ESPHome/Voice PE
  // negotiate - cheap for the server to encode, unlike realtime MP3);
  // WAV/PCM covers announcements; MP3 covers TTS proxies and radio.
  uint8_t hdr[12] = {0};
  _ring.peek(hdr, sizeof(hdr));
  const char* codec = "mp3";
  _dec = makeDecoder(hdr, sizeof(hdr), &codec);
  _src = new RingSource(&_ring, &_stopReq);
  if (!DEC->begin(SRC, OUT)) {
    Serial.println("[media] decoder begin failed");
    stopMedia();
    _streaming = false;
    return;
  }
  _playing = true;
  _live = true;
  Serial.printf("[media] streaming %s (%s, prefill %uKB, len %lld)\n", url,
                codec, (unsigned)(_ring.fill() / 1024),
                (long long)_reader.contentLength());
}

// Tear down the media pipeline in dependency order: reader first (producer
// parks; it only touches the ring, never _dec/_src), then decoder, then the
// ring reset - both sides are quiescent by then. Decoder task only.
void AudioPlayer::stopMedia() {
  _reader.stop();
  const uint32_t t0 = millis();
  while (!_reader.idle() && millis() - t0 < 2500) vTaskDelay(1);
  if (!_reader.idle()) Serial.println("[media] reader stop timeout");  // shouldn't happen
  cleanupDecoder();
  _ring.reset();
  _live = false;
  _playing = false;  // _dec is gone; a stale true would null-deref DEC->loop()
}

void AudioPlayer::cleanupDecoder() {
  if (_dec) { DEC->stop(); delete DEC; _dec = nullptr; }
  if (_src) { delete SRC; _src = nullptr; }
}

void AudioPlayer::taskTrampoline(void* arg) {
  static_cast<AudioPlayer*>(arg)->taskLoop();
}

void AudioPlayer::taskLoop() {
  for (;;) {
    // Snapshot the pending requests atomically, then act outside the lock.
    const unsigned char* data = nullptr;
    unsigned int len = 0;
    bool streamNow = false, stopNow = false;
    char url[sizeof(_reqUrl)];
    portENTER_CRITICAL(&_reqMux);
    if (_stopReq) { _stopReq = false; stopNow = true; }
    if (_streamReq) {
      _streamReq = false;
      streamNow = true;
      memcpy(url, _reqUrl, sizeof(url));
    } else if (_startReq) {
      _startReq = false;
      data = _reqData;
      len = _reqLen;
    }
    portEXIT_CRITICAL(&_reqMux);

    if (stopNow && _streaming) {
      stopMedia();
      _playing = false;
      _streaming = false;
      Serial.println("[media] stopped");
    }
    if (streamNow) startMedia(url);
    else if (data) startDecode(data, len);

    if (_playing) {
      if (!DEC->loop()) {  // clip finished / EOF (or a live stream dropped)
        if (_live) {
          // Diagnose why media ended: fill>0 = decoder choked on the data;
          // fill==0 = the source dried up (clean EOF or dead server).
          Serial.printf("[media] end: fill=%u\n", (unsigned)_ring.fill());
          stopMedia();
        } else {
          cleanupDecoder();
        }
        _playing = false;
        if (_streaming) {
          _streaming = false;
          Serial.println("[media] ended");
        }
      } else if (_live) {
        // The reader task fills the ring; here we only profile its health.
        static uint32_t lastStat = 0;
        const uint32_t nowMs = millis();
        if (nowMs - lastStat > 2000) {
          lastStat = nowMs;
          Serial.printf("[media] ring fill=%uKB min=%uKB underruns=%u net=%uKB/s\n",
                        (unsigned)(_ring.fill() / 1024),
                        (unsigned)(_ring.minFill() / 1024),
                        (unsigned)_ring.underruns(),
                        (unsigned)(_ring.bytesIn() / 1024 / 2));  // 2s window
          _ring.statsReset();
        }
      }
    }
    vTaskDelay(1);  // yield (1 tick)
  }
}
