#include "AudioCodec.h"
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

// ---- CodecOutput (AudioOutput sink for the decoders) ----

bool CodecOutput::SetRate(int hz) {
  hertz = hz;
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
