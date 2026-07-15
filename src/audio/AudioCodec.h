#pragma once
#include <Arduino.h>
#include <AudioOutput.h>

// ============================================================
// AudioCodec: owns the ONE full-duplex I2S driver on port 0.
//
// The ES8311 is a single codec on a single clock domain, so its
// DAC (playback, DOUT->GPIO8) and ADC (mic, DIN->GPIO10) share
// the same BCLK/LRCLK/MCLK. We therefore install ONE full-duplex
// (MASTER|TX|RX) legacy-driver instance and drive both directions
// through it.
//
// HALF-DUPLEX policy: playback retunes the I2S clock to the
// decoded rate (44.1/48kHz); the mic pipeline wants 16kHz mono.
// You cannot capture 16kHz while playing 48kHz on one port, so
// the capture task is suspended during playback and the clock is
// restored to 16kHz afterwards (AudioPlayer coordinates this).
//
// Replaces ESP8266Audio's AudioOutputI2S, which installs the
// driver TX-only and can't be pointed at a pre-installed one.
// The TX config mirrors AudioOutputI2S exactly (the proven-good
// playback path: MCLK on GPIO16, APLL, same DMA sizing) so audio
// quality is unchanged; we only add RX + tx auto-clear.
// ============================================================
namespace AudioCodec {

// Install the full-duplex I2S driver (idle rate = 16kHz stereo 16-bit).
// Call ONCE at boot, after es8311 I2C init. Returns false on driver error.
bool begin();

// Restore the 16kHz capture clock after playback retuned it. Cheap; safe to
// call when already at 16kHz.
void setCaptureRate();

// Blocking read of one mic frame -> mono. Reads stereo 16-bit from the ADC
// slot and keeps one channel. Returns the number of mono samples written
// (0 on timeout). `frames` is the mono sample count wanted.
size_t readMicMono(int16_t* dst, size_t frames, uint32_t timeoutMs);

}  // namespace AudioCodec

// AudioOutput sink the ESP8266Audio decoders write into. Delegates to the
// shared full-duplex driver; SetRate retunes the TX clock (playback).
class CodecOutput : public AudioOutput {
 public:
  bool begin() override { return true; }  // driver already installed
  bool SetRate(int hz) override;
  bool SetBitsPerSample(int bits) override { bps = bits; return true; }
  bool SetChannels(int chan) override { channels = chan; return true; }
  bool ConsumeSample(int16_t sample[2]) override;
  bool stop() override;
};
