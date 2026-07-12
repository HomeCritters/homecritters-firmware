#include "AudioPlayer.h"
#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioOutputI2S.h>
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
  };
  for (auto& s : seq) esWrite(s[0], s[1]);
}

}  // namespace

// Convenience casts for the opaque pointers stored in the header.
#define DEC (static_cast<AudioGenerator*>(_dec))
#define SRC (static_cast<AudioFileSource*>(_src))
#define HTTPSRC (static_cast<AudioFileSource*>(_http))
#define OUT (static_cast<AudioOutputI2S*>(_out))

// Network ring buffer for streaming (PSRAM): absorbs WiFi hiccups.
static constexpr uint32_t STREAM_BUF_BYTES = 64 * 1024;

void AudioPlayer::begin() {
  // I2C bus "A" dedicated to the codec (the touch uses bus 1).
  Wire.begin(PIN_I2C_A_SDA, PIN_I2C_A_SCL, 400000);
  es8311_init();

  // Enable the speaker amplifier.
  pinMode(PIN_SPEAKER_EN, OUTPUT);
  digitalWrite(PIN_SPEAKER_EN, HIGH);

  // I2S output to the codec (ESP32 is the I2S bus master).
  auto* out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 16);
  out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
  _out = out;

  // Saved volume (default 80%).
  Preferences p;
  p.begin("audio", true);
  _volume = p.getInt("vol", 80);
  p.end();
  applyGain();

  // ESP8266Audio's HTTP source waits for data with yield()-spins, which never
  // let the priority-0 idle task run - streaming bursts on core 0 would trip
  // the idle task watchdog. Unwatch IDLE0 (core 1 / render stays protected).
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));

  // 12KB stack: the HTTP streaming source (WiFiClient + headers) needs more
  // headroom than PROGMEM playback.
  xTaskCreatePinnedToCore(taskTrampoline, "audio", 12288, this, 2, nullptr, 0);
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

void AudioPlayer::playSimon(int color) {
  switch (color) {
    case 0: play(simon_green_wav,  simon_green_wav_len);  break;
    case 1: play(simon_red_wav,    simon_red_wav_len);    break;
    case 2: play(simon_yellow_wav, simon_yellow_wav_len); break;
    case 3: play(simon_blue_wav,   simon_blue_wav_len);   break;
  }
}

void AudioPlayer::startDecode(const unsigned char* data, unsigned int len) {
  cleanup();
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

// Open an HTTP MP3 stream: net source -> PSRAM ring buffer -> MP3 decoder.
// Runs on the audio task (network reads never touch the render loop).
void AudioPlayer::startStream(const char* url) {
  cleanup();
  _streaming = false;
  if (!_streamBuf) {
    _streamBuf = (uint8_t*)ps_malloc(STREAM_BUF_BYTES);
    if (!_streamBuf) _streamBuf = (uint8_t*)malloc(16 * 1024);
    if (!_streamBuf) { Serial.println("[media] no buffer memory"); return; }
  }
  auto* http = new AudioFileSourceHTTPStream();
  if (!http->open(url)) {
    Serial.printf("[media] open failed: %s\n", url);
    delete http;
    return;
  }
  _http = http;
  _src = new AudioFileSourceBuffer(http, _streamBuf, STREAM_BUF_BYTES);
  _dec = new AudioGeneratorMP3();
  if (!DEC->begin(SRC, OUT)) {
    Serial.println("[media] decoder begin failed");
    cleanup();
    return;
  }
  _playing = true;
  _streaming = true;
  Serial.printf("[media] streaming %s\n", url);
}

void AudioPlayer::cleanup() {
  if (_dec) { DEC->stop(); delete DEC; _dec = nullptr; }
  if (_src) { delete SRC; _src = nullptr; }
  if (_http) { delete HTTPSRC; _http = nullptr; }
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
      cleanup();
      _playing = false;
      _streaming = false;
      Serial.println("[media] stopped");
    }
    if (streamNow) startStream(url);
    else if (data) startDecode(data, len);

    if (_playing) {
      if (!DEC->loop()) {  // clip/stream finished (or the stream dropped)
        cleanup();
        _playing = false;
        if (_streaming) {
          _streaming = false;
          Serial.println("[media] stream ended");
        }
      }
    }
    vTaskDelay(1);  // yield (1 tick)
  }
}
