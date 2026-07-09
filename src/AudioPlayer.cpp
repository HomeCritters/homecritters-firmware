#include "AudioPlayer.h"
#include <Wire.h>
#include <Preferences.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioOutputI2S.h>
#include "pins.h"
#include "sleep_music.h"
#include "sfx_eat.h"
#include "sfx_drink.h"
#include "sfx_tap.h"
#include "sfx_wake.h"
#include "sfx_jump.h"
#include "sfx_boost.h"
#include "sfx_death.h"

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
#define MP3 (static_cast<AudioGeneratorMP3*>(_mp3))
#define SRC (static_cast<AudioFileSourcePROGMEM*>(_src))
#define OUT (static_cast<AudioOutputI2S*>(_out))

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

  xTaskCreatePinnedToCore(taskTrampoline, "audio", 8192, this, 2, nullptr, 0);
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

// Request a clip (preempts the current one). Non-blocking.
void AudioPlayer::play(const unsigned char* data, unsigned int len) {
  _reqData = data;
  _reqLen = len;
  _startReq = true;
}

void AudioPlayer::playSleepTune() { play(sleep_music_mp3, sleep_music_mp3_len); }
void AudioPlayer::playWake()      { play(sfx_wake_mp3,     sfx_wake_mp3_len); }
void AudioPlayer::playEat()       { play(sfx_eat_mp3,      sfx_eat_mp3_len); }
void AudioPlayer::playDrink()     { play(sfx_drink_mp3,    sfx_drink_mp3_len); }
void AudioPlayer::playPat()       { play(sfx_tap_mp3,      sfx_tap_mp3_len); }
void AudioPlayer::playJump()      { play(sfx_jump_mp3,     sfx_jump_mp3_len); }
void AudioPlayer::playBoost()     { play(sfx_boost_mp3,    sfx_boost_mp3_len); }
void AudioPlayer::playDeath()     { play(sfx_death_mp3,    sfx_death_mp3_len); }
void AudioPlayer::stop()          { _stopReq = true; }

void AudioPlayer::startDecode() {
  cleanup();
  if (!_reqData || _reqLen == 0) return;
  _src = new AudioFileSourcePROGMEM(_reqData, _reqLen);
  _mp3 = new AudioGeneratorMP3();
  MP3->begin(SRC, OUT);
  _playing = true;
}

void AudioPlayer::cleanup() {
  if (_mp3) { MP3->stop(); delete MP3; _mp3 = nullptr; }
  if (_src) { delete SRC; _src = nullptr; }
}

void AudioPlayer::taskTrampoline(void* arg) {
  static_cast<AudioPlayer*>(arg)->taskLoop();
}

void AudioPlayer::taskLoop() {
  for (;;) {
    if (_stopReq) {
      cleanup();
      _playing = false;
      _stopReq = false;
    }
    if (_startReq) {
      _startReq = false;
      startDecode();
    }
    if (_playing) {
      if (!MP3->loop()) {  // clip finished
        cleanup();
        _playing = false;
      }
    }
    vTaskDelay(1);  // yield (1 tick)
  }
}
