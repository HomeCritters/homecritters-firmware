#include "AudioCodec.h"
#include <atomic>
#include <driver/i2s.h>
#include "pins.h"

// Everything shares I2S port 0 (full-duplex). CodecOutput lives in this TU so
// it can reference the same port/rate constants directly.
static constexpr i2s_port_t PORT = I2S_NUM_0;
static constexpr uint32_t CAPTURE_RATE = 16000;  // mic / Assist pipeline rate
static bool s_installed = false;

bool AudioCodec::begin() {
  if (s_installed) return true;

  // TX half mirrors ESP8266Audio's AudioOutputI2S (the proven-good playback
  // path); we only add RX (MASTER|TX|RX) + data_in_num for the mic. MCLK stays
  // on GPIO16 - regressing to GPIO0 desensed WiFi (documented lesson).
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  cfg.sample_rate = CAPTURE_RATE;                       // idle = capture rate
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;      // stereo frames both ways
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 16;
  cfg.dma_buf_len = 128;
  cfg.use_apll = true;
  cfg.tx_desc_auto_clear = true;                        // silence on TX underflow
  cfg.fixed_mclk = 0;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;
#endif
  if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  pins.mck_io_num = PIN_I2S_MCLK;   // GPIO16 - keep!
#endif
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCLK;
  pins.data_out_num = PIN_I2S_DOUT;  // speaker (DAC)
  pins.data_in_num = PIN_I2S_DIN;    // mic (ADC)
  if (i2s_set_pin(PORT, &pins) != ESP_OK) return false;

  i2s_zero_dma_buffer(PORT);
  s_installed = true;
  return true;
}

void AudioCodec::setCaptureRate() {
  if (s_installed)
    i2s_set_clk(PORT, CAPTURE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

size_t AudioCodec::readMicMono(int16_t* dst, size_t frames, uint32_t timeoutMs) {
  if (!s_installed) return 0;
  int16_t stereo[256 * 2];  // up to 256 stereo frames per i2s_read
  size_t done = 0;
  while (done < frames) {
    size_t want = frames - done;
    if (want > 256) want = 256;
    size_t bytesRead = 0;
    if (i2s_read(PORT, stereo, want * 2 * sizeof(int16_t), &bytesRead,
                 pdMS_TO_TICKS(timeoutMs)) != ESP_OK)
      break;
    const size_t got = bytesRead / (2 * sizeof(int16_t));
    if (got == 0) break;
    // Keep the left slot (the ES8311 ADC lands on one channel; verified in
    // bring-up - if left is silent, switch to stereo[i*2+1]).
    for (size_t i = 0; i < got; i++) dst[done + i] = stereo[i * 2];
    done += got;
  }
  return done;
}

// Walkie-talkie RX: raw mono 16k -> gain -> stereo -> DMA. Runs on the audio
// task (the walkie drain); blocking writes in small chunks so a session can
// stop quickly. Feeds the same level meter the party LED reads.
size_t AudioCodec::playRaw16(const int16_t* mono, size_t n, uint16_t gainQ12) {
  if (!s_installed) return 0;
  int16_t stereo[64 * 2];
  size_t done = 0;
  while (done < n) {
    size_t chunk = n - done;
    if (chunk > 64) chunk = 64;
    for (size_t i = 0; i < chunk; i++) {
      int32_t v = ((int32_t)mono[done + i] * gainQ12) >> 12;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      stereo[i * 2] = (int16_t)v;
      stereo[i * 2 + 1] = (int16_t)v;
    }
    size_t written = 0;
    if (i2s_write(PORT, stereo, chunk * 2 * sizeof(int16_t), &written,
                  pdMS_TO_TICKS(100)) != ESP_OK)
      break;
    done += written / (2 * sizeof(int16_t));
    if (written == 0) break;
  }
  return done;
}

// ---- SFX overlay (the mixer) ----
// One PCM overlay summed on top of the decoded stream inside ConsumeSample.
// Writer = render loop (startOverlay), reader = audio task. s_ovlActive is
// the release/acquire gate for the payload fields; position advances only on
// the audio task. PROGMEM source is fine: S3 flash is memory-mapped, but we
// still read through pgm_read_word for correctness.
static const uint8_t* s_ovlPcm = nullptr;  // sample data (PCM16 LE mono)
static uint32_t s_ovlN = 0;                // total samples
static uint32_t s_ovlPosFp = 0;            // playhead, 16.16 fixed point
static uint32_t s_ovlStepFp = 1 << 16;     // sfxRate/outRate, 16.16
static uint32_t s_ovlRate = 16000;         // source rate (for SetRate rescale)
static uint8_t s_ovlGain = 230;            // 0..255 (~0.9)
static std::atomic<bool> s_ovlActive{false};
static int s_outHz = 16000;                // current TX clock (SetRate)

bool AudioCodec::startOverlay(const uint8_t* wav, uint32_t len, uint8_t gain) {
  // Proper RIFF chunk walk: afconvert (and others) insert extra chunks (a
  // 4KB "FLLR" filler, LIST metadata...) between fmt and data. Assuming the
  // canonical 44-byte layout made the overlay "play" the filler: ~0.13s of
  // silence - thunder/clicks were inaudible over music while the Genius
  // tones (canonical headers) mixed fine.
  if (len < 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0)
    return false;
  auto rd16 = [&](uint32_t o) { return (uint16_t)(wav[o] | (wav[o + 1] << 8)); };
  auto rd32 = [&](uint32_t o) {
    return (uint32_t)wav[o] | ((uint32_t)wav[o + 1] << 8) |
           ((uint32_t)wav[o + 2] << 16) | ((uint32_t)wav[o + 3] << 24);
  };
  uint32_t rate = 0, dataOff = 0, dataLen = 0;
  bool fmtOk = false;
  for (uint32_t p = 12; p + 8 <= len;) {
    const uint32_t sz = rd32(p + 4);
    if (memcmp(wav + p, "fmt ", 4) == 0 && sz >= 16 && p + 8 + 16 <= len) {
      fmtOk = rd16(p + 8) == 1 && rd16(p + 10) == 1 && rd16(p + 22) == 16;
      rate = rd32(p + 12);  // PCM, mono, 16-bit
    } else if (memcmp(wav + p, "data", 4) == 0) {
      dataOff = p + 8;
      dataLen = sz;
      break;
    }
    p += 8 + sz + (sz & 1);  // chunks are word-aligned
  }
  if (!fmtOk || !rate || !dataOff) return false;
  if (dataLen > len - dataOff) dataLen = len - dataOff;
  const uint32_t n = dataLen / 2;
  if (!n) return false;

  s_ovlActive.store(false);  // park the reader while the payload swaps
  s_ovlPcm = wav + dataOff;
  s_ovlN = n;
  s_ovlPosFp = 0;
  s_ovlRate = rate;
  s_ovlGain = gain;
  const int hz = s_outHz > 0 ? s_outHz : 16000;
  s_ovlStepFp = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)hz);
  s_ovlActive.store(true, std::memory_order_release);
  return true;
}

void AudioCodec::stopOverlay() { s_ovlActive.store(false); }
bool AudioCodec::overlayActive() { return s_ovlActive.load(); }

// ---- CodecOutput (AudioOutput sink for the decoders) ----

bool CodecOutput::SetRate(int hz) {
  hertz = hz;
  s_outHz = hz;
  // Mid-overlay rate change (media track switched): keep the SFX pitch right.
  if (s_ovlActive.load(std::memory_order_acquire) && hz > 0)
    s_ovlStepFp = (uint32_t)(((uint64_t)s_ovlRate << 16) / (uint32_t)hz);
  // Playback retunes the shared clock; capture is suspended meanwhile.
  i2s_set_clk(PORT, hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  return true;
}

// Rolling output level meter (see AudioCodec::outLevel). Written only by the
// decoder task; read by the render loop (16-bit, atomic on ESP32).
static volatile uint16_t s_outLevel = 0;
static uint32_t s_lvlAcc = 0;
static uint16_t s_lvlN = 0;

uint16_t AudioCodec::outLevel() { return s_outLevel; }

bool CodecOutput::ConsumeSample(int16_t sample[2]) {
  int16_t ms[2] = {sample[0], sample[1]};
  MakeSampleStereo16(ms);
  // Mixer: sum the active SFX overlay BEFORE Amplify, so it follows the
  // master volume exactly like the media does. Saturating add.
  if (s_ovlActive.load(std::memory_order_acquire)) {
    const uint32_t i = s_ovlPosFp >> 16;
    if (i >= s_ovlN) {
      s_ovlActive.store(false);  // overlay finished
    } else {
      const int16_t raw = (int16_t)pgm_read_word(s_ovlPcm + i * 2);
      const int32_t o = ((int32_t)raw * s_ovlGain) >> 8;
      int32_t l = ms[LEFTCHANNEL] + o, r = ms[RIGHTCHANNEL] + o;
      ms[LEFTCHANNEL] = l < -32768 ? -32768 : l > 32767 ? 32767 : (int16_t)l;
      ms[RIGHTCHANNEL] = r < -32768 ? -32768 : r > 32767 ? 32767 : (int16_t)r;
      s_ovlPosFp += s_ovlStepFp;
    }
  }
  const int16_t al = Amplify(ms[LEFTCHANNEL]);
  const int16_t ar = Amplify(ms[RIGHTCHANNEL]);
  const uint32_t s32 = ((uint32_t)(uint16_t)ar << 16) | ((uint16_t)al & 0xffff);
  size_t written = 0;
  i2s_write(PORT, &s32, sizeof(s32), &written, 0);  // non-blocking (decoder paces)
  if (written) {
    // Level meter: mean |left| over 512-sample windows (~12ms @ 44.1kHz).
    s_lvlAcc += al < 0 ? -al : al;
    if (++s_lvlN >= 512) {
      s_outLevel = (uint16_t)(s_lvlAcc / 512);
      s_lvlAcc = 0;
      s_lvlN = 0;
    }
  }
  return written != 0;
}

bool CodecOutput::stop() {
  if (s_installed) i2s_zero_dma_buffer(PORT);
  return true;
}
