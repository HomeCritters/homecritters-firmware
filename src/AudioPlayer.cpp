#include "AudioPlayer.h"
#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioGeneratorFLAC.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceHTTPStream.h>
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
#define HTTPSRC (static_cast<AudioFileSourceHTTPStream*>(_http))
#define OUT (static_cast<AudioOutputI2S*>(_out))

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

// Network ring buffer for live streaming (PSRAM): absorbs jitter and lets a
// faster-than-realtime source build a cushion. 256KB is ~16s @128kbps / ~6s
// @320kbps of runway.
static constexpr uint32_t STREAM_BUF_BYTES = 256 * 1024;

// ------------------------------------------------------------
// StreamRingSource: PSRAM ring between the HTTP source and the decoder.
//
// Why not AudioFileSourceBuffer: on underflow it falls back to the HTTP
// source's BLOCKING read, which waits only 500ms for data and then KILLS the
// connection (http.end()) and reports EOF. Sources that pace their output
// (Music Assistant transcodes, some radios) routinely gap longer than that,
// so playback died seconds in. This ring only ever uses readNonBlock() -
// which never closes the connection - and read() waits patiently for data:
// an underrun becomes a brief silence instead of end-of-song.
// ------------------------------------------------------------
class StreamRingSource : public AudioFileSource {
 public:
  // Underrun grace: how long read() waits for new bytes before declaring the
  // stream dead. Generous on purpose - real disconnects are detected earlier
  // via isOpen() (TCP closed), so this only limits a silent, stuck server.
  static constexpr uint32_t READ_GRACE_MS = 15000;

  // abortFlag: checked while read() waits for data, so a pending stop request
  // interrupts the wait instead of hanging the audio task for the grace time.
  StreamRingSource(AudioFileSourceHTTPStream* http, uint8_t* buf, uint32_t cap,
                   volatile bool* abortFlag)
      : _http(http), _buf(buf), _cap(cap), _abort(abortFlag) {}

  // Producer side (net task ONLY). Pulls whatever TCP already has into the
  // ring; never blocks. Draining continuously here keeps TCP ACKs flowing so
  // the receive window stays open - that's what unlocks 256/320kbps.
  bool loop() override {
    if (_eof) return true;
    for (;;) {
      const uint32_t used = (uint32_t)(_w - _r);
      const uint32_t space = _cap - used;
      if (space == 0) return true;  // ring full
      const uint32_t at = _w % _cap;
      uint32_t chunk = min(space, _cap - at);  // contiguous till wrap
      chunk = min(chunk, (uint32_t)8192);
      const uint32_t n = _http->readNonBlock(_buf + at, chunk);
      if (n == 0) {
        // No data right now. If the connection is really gone AND drained,
        // that's the true end of the stream.
        if (!_http->isOpen()) _eof = true;
        return true;
      }
      _w += n;
      _bytesIn += n;
    }
  }

  // Consumer side (decode task). Does NOT fill - the producer task owns loop().
  // It only waits for the producer to advance _w. Single-producer/single-
  // consumer: producer writes _w, consumer writes _r, both 32-bit atomic reads.
  //
  // Fills the FULL len before returning (like a file source), waiting for the
  // producer as needed. Partial reads made AudioGeneratorWAV misparse the RIFF
  // header (wrong sample rate -> ~1/4-speed "morse" playback); AudioGenerator
  // MP3 tolerates short reads but full reads are correct for both. Returns
  // fewer than len only at EOF / abort / underrun-grace.
  uint32_t read(void* data, uint32_t len) override {
    uint32_t got = 0;
    uint32_t t0 = millis();
    bool waited = false;
    while (got < len) {
      const uint32_t avail = (uint32_t)(_w - _r);
      if (avail > 0) {
        if (waited) { _underruns++; waited = false; }
        if (avail < _minFill) _minFill = avail;
        const uint32_t want = min(avail, len - got);
        const uint32_t at = _r % _cap;
        const uint32_t first = min(want, _cap - at);
        memcpy((uint8_t*)data + got, _buf + at, first);
        if (want > first) memcpy((uint8_t*)data + got + first, _buf, want - first);
        _r += want;
        got += want;
        t0 = millis();  // progress: reset the underrun grace window
        continue;
      }
      if (_eof) break;
      if (_abort && *_abort) break;  // stop requested: return what we have
      if (millis() - t0 > READ_GRACE_MS) { _eof = true; break; }
      waited = true;
      vTaskDelay(1);  // wait for the producer to deliver more bytes
    }
    return got;
  }

  // Copy up to n buffered bytes WITHOUT consuming them (to sniff the header
  // and pick the decoder). Returns how many were copied.
  uint32_t peek(uint8_t* out, uint32_t n) const {
    const uint32_t avail = (uint32_t)(_w - _r);
    n = min(n, avail);
    const uint32_t at = _r % _cap;
    const uint32_t first = min(n, _cap - at);
    memcpy(out, _buf + at, first);
    if (n > first) memcpy(out + first, _buf, n - first);
    return n;
  }

  uint32_t fillLevel() const { return (uint32_t)(_w - _r); }
  bool full() const { return (uint32_t)(_w - _r) >= _cap; }
  bool endOfStream() const { return _eof; }

  // Rolling stats for profiling; statsReset() clears the window.
  uint32_t underruns() const { return _underruns; }
  uint32_t minFill() const { return _minFill; }
  uint32_t bytesIn() const { return _bytesIn; }
  void statsReset() { _underruns = 0; _minFill = _cap; _bytesIn = 0; }

  bool seek(int32_t, int) override { return false; }
  bool close() override { return _http->close(); }
  bool isOpen() override { return true; }
  // Report an "infinite" length. AudioGeneratorFLAC's eof callback is
  // `getPos() >= getSize()`, so getSize()==0 made it declare EOF on the very
  // first frame (FLAC aborted instantly). Real end-of-stream is still detected
  // by read() returning 0. getPos stays well under this for hours of playback.
  uint32_t getSize() override { return 0x7FFFFFFF; }
  uint32_t getPos() override { return (uint32_t)_r; }

 private:
  AudioFileSourceHTTPStream* _http;  // owned by AudioPlayer (_http member)
  uint8_t* _buf;
  uint32_t _cap;
  volatile bool* _abort;
  // Byte counters (index = counter % cap). uint32_t wraps cleanly for the
  // difference math and is atomically read/written on the ESP32, so the
  // producer (_w) and consumer (_r) can run on separate tasks lock-free.
  volatile uint32_t _w = 0, _r = 0;
  volatile bool _eof = false;
  uint32_t _underruns = 0, _minFill = 0xFFFFFFFF, _bytesIn = 0;
};

// Finite media (TTS, announcements, songs) is downloaded whole into PSRAM and
// played from memory. Cap the download so a huge/endless URL can't exhaust
// PSRAM; anything bigger (or unknown-length) falls back to live streaming.
// 4MB is ~4 min at 128kbps - covers TTS and typical Music Assistant tracks.
static constexpr uint32_t MEDIA_DL_MAX = 4u * 1024 * 1024;

// Streaming: how much to buffer BEFORE starting the decoder. A deep prefill
// gives the biggest possible head start against underruns (the decoder starts
// with seconds of audio banked), at the cost of a longer startup delay.
// 96KB = ~6s @128kbps / ~2.4s @320kbps. The prefill loop bails early if the
// source plateaus, so short/slow clips don't wait the whole budget.
static constexpr uint32_t STREAM_PREFILL_BYTES = 96u * 1024;

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

  // Two-core split, mirroring ESPHome's reader/decoder pipeline:
  //   - DECODER on core 1 (APP). Heavy codecs (FLAC especially) are CPU-bound;
  //     on core 0 the decode starved the WiFi stack + the net producer, capping
  //     the pull rate. Core 1 owns the render loop, but the scene render is
  //     frozen while audio streams (see main.cpp), so core 1 has the headroom.
  //   - NET PRODUCER on core 0 (PRO, with WiFi/lwIP). Pure I/O: drains TCP into
  //     the ring, keeping ACKs flowing so the receive window stays open.
  // Each core's busy task can starve its idle task, so unwatch both WDTs.
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

  // 12KB stack: the HTTP source (WiFiClient + headers) needs more headroom
  // than PROGMEM playback.
  _srcMux = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskTrampoline, "audio", 12288, this, 2, nullptr, 1);
  xTaskCreatePinnedToCore(netTaskTrampoline, "audionet", 6144, this, 2, nullptr, 0);
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
// One URL, ONE connection (TTS/Music Assistant flow URLs are single-use - a
// probe GET would consume them). Open it once, then decide by Content-Length:
//  - finite + fits PSRAM -> download fully over this connection, play from
//    memory (clean start/end, WiFi completely free during playback);
//  - unknown length / oversized (chunked TTS proxy, radio, MA flows) ->
//    buffered live streaming over this same connection, with a prefill so the
//    decoder never starts on an empty ring (that chopped the first second).
void AudioPlayer::startMedia(const char* url) {
  cleanup();
  _live = false;
  // NOTE: _streaming is already true (set optimistically by playStream() so
  // the reported state flips to "playing" instantly - Music Assistant gives
  // up if the player stays "idle" after a play command). Only clear it on
  // the failure paths below; don't reset it here or the state blips idle.
  if (strncmp(url, "http://", 7) != 0) {  // ESP32 has no TLS here
    Serial.printf("[media] unsupported url: %s\n", url);
    _streaming = false;
    return;
  }
  auto* http = new AudioFileSourceHTTPStream();
  if (!http->open(url)) {
    Serial.printf("[media] open failed: %s\n", url);
    delete http;
    _streaming = false;
    return;
  }
  _http = http;

  const uint32_t size = http->getSize();  // 0 / huge when chunked or unknown
  if (size > 0 && size <= MEDIA_DL_MAX && downloadToPsram(http, size)) {
    delete HTTPSRC;  // download complete; drop the connection
    _http = nullptr;
    _src = new AudioFileSourcePROGMEM(_dlBuf, _dlLen);
    const char* codec = "mp3";
    _dec = makeDecoder(_dlBuf, _dlLen, &codec);
    if (!DEC->begin(SRC, OUT)) { cleanup(); _streaming = false; return; }
    _playing = true;
    _streaming = true;  // "media active" for state + SFX suppression
    Serial.printf("[media] playing %u bytes from PSRAM (%s)\n", _dlLen, codec);
    return;
  }
  startStream(url);  // reuses _http (already open)
}

// Pull a known-length body into PSRAM through the already-open source.
bool AudioPlayer::downloadToPsram(void* httpv, uint32_t size) {
  auto* http = static_cast<AudioFileSourceHTTPStream*>(httpv);
  uint8_t* buf = (uint8_t*)ps_malloc(size);
  if (!buf) return false;
  uint32_t got = 0, last = millis();
  while (got < size) {
    uint32_t n = http->readNonBlock(buf + got, min((uint32_t)4096, size - got));
    if (n > 0) {
      got += n;
      last = millis();
    } else if (millis() - last > 8000) {
      break;  // stalled
    } else {
      vTaskDelay(1);  // yield to WiFi while waiting for the next TCP chunk
    }
  }
  if (got != size) { free(buf); return false; }
  _dlBuf = buf;
  _dlLen = size;
  return true;
}

// Live streaming over the connection startMedia() already opened in _http.
// The url parameter is only for logging.
void AudioPlayer::startStream(const char* url) {
  if (!_http) { _streaming = false; return; }  // startMedia() always sets it
  if (!_streamBuf) {
    _streamBuf = (uint8_t*)ps_malloc(STREAM_BUF_BYTES);
    if (!_streamBuf) _streamBuf = (uint8_t*)malloc(16 * 1024);
    if (!_streamBuf) {
      Serial.println("[media] no buffer memory");
      cleanup();
      _streaming = false;
      return;
    }
  }
  auto* buf = new StreamRingSource(HTTPSRC, _streamBuf, STREAM_BUF_BYTES, &_stopReq);
  _src = buf;
  // Prefill the ring before starting the decoder: an empty buffer chopped the
  // first second of TTS. Stop early when the level plateaus (small clip hit
  // EOF, or the server is slow - start with what we have) or on the deadline.
  const uint32_t t0 = millis();
  uint32_t lastLvl = 0, lastRise = t0;
  for (;;) {
    buf->loop();  // non-blocking fill from TCP
    const uint32_t lvl = buf->fillLevel();
    if (lvl >= STREAM_PREFILL_BYTES || buf->endOfStream()) break;
    if (lvl != lastLvl) { lastLvl = lvl; lastRise = millis(); }
    else if (millis() - lastRise > 1500) break;  // truly stalled: go with what we have
    if (millis() - t0 > 8000) break;  // hard cap on startup delay
    vTaskDelay(1);
  }
  // Sniff the buffered header to pick the decoder. FLAC ("fLaC") is what Music
  // Assistant sends for lossless media (the format ESPHome speakers negotiate):
  // half the bandwidth of WAV and cheap for the server to encode, unlike
  // realtime 320k MP3. WAV/PCM covers announcements; MP3 covers TTS and radio.
  uint8_t hdr[12] = {0};
  buf->peek(hdr, sizeof(hdr));
  const char* codec = "mp3";
  _dec = makeDecoder(hdr, sizeof(hdr), &codec);
  if (!DEC->begin(SRC, OUT)) {
    Serial.println("[media] decoder begin failed");
    cleanup();
    _streaming = false;
    return;
  }
  _playing = true;
  _streaming = true;
  _live = true;  // ring buffer needs on-going refill in taskLoop
  Serial.printf("[media] streaming %s (%s, prefill %uB)\n", url, codec,
                (unsigned)buf->fillLevel());
}

void AudioPlayer::cleanup() {
  // Exclude the producer task (netTaskLoop) while we tear down the source it
  // fills. Decoder + download buffer are consumer-only, so they stay outside.
  if (_dec) { DEC->stop(); delete DEC; _dec = nullptr; }
  if (_srcMux) xSemaphoreTake(_srcMux, portMAX_DELAY);
  if (_src) { delete SRC; _src = nullptr; }
  if (_http) { delete HTTPSRC; _http = nullptr; }
  if (_srcMux) xSemaphoreGive(_srcMux);
  if (_dlBuf) { free(_dlBuf); _dlBuf = nullptr; _dlLen = 0; }
}

void AudioPlayer::taskTrampoline(void* arg) {
  static_cast<AudioPlayer*>(arg)->taskLoop();
}

void AudioPlayer::netTaskTrampoline(void* arg) {
  static_cast<AudioPlayer*>(arg)->netTaskLoop();
}

// Producer: while a live stream is playing, keep the ring as full as TCP
// allows. Naps only when the ring is full or no data is pending; otherwise
// spins tightly so the receive window never idles.
void AudioPlayer::netTaskLoop() {
  for (;;) {
    if (!(_live && _playing && _src)) { vTaskDelay(5); continue; }
    // Hold _srcMux across the fill so cleanup() (consumer task) can't delete
    // _src out from under us - without this, stopping a live stream was a
    // use-after-free that hung the audio task (playback stuck, stop ignored).
    bool nap = true;
    xSemaphoreTake(_srcMux, portMAX_DELAY);
    if (_live && _playing && _src) {
      auto* ring = static_cast<StreamRingSource*>(_src);
      const uint32_t before = ring->fillLevel();
      ring->loop();  // drain everything currently available
      nap = ring->full() || ring->endOfStream() || ring->fillLevel() == before;
    }
    xSemaphoreGive(_srcMux);
    if (nap) vTaskDelay(1);  // ring full / done / nothing arrived: nap a tick
    else taskYIELD();        // made progress, more may be waiting: come back
  }
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
      _live = false;
      Serial.println("[media] stopped");
    }
    if (streamNow) startMedia(url);
    else if (data) startDecode(data, len);

    if (_playing) {
      if (!DEC->loop()) {  // clip finished / EOF (or a live stream dropped)
        if (_live && _src) {
          // Diagnose why a live stream ended: fill>0 = decoder choked on the
          // data; fill==0 = the source dried up (server stopped/closed).
          Serial.printf("[media] live end: fill=%u pos=%u\n",
                        (unsigned)static_cast<StreamRingSource*>(_src)->fillLevel(),
                        (unsigned)(_http ? HTTPSRC->getPos() : 0));
        }
        cleanup();
        _playing = false;
        if (_streaming) {
          _streaming = false;
          _live = false;
          Serial.println("[media] ended");
        }
      } else if (_live && _src) {
        // The net task fills the ring now (see netTask); here we only profile.
        auto* ring = static_cast<StreamRingSource*>(_src);
        static uint32_t lastStat = 0;
        const uint32_t nowMs = millis();
        if (nowMs - lastStat > 2000) {
          lastStat = nowMs;
          Serial.printf("[media] ring fill=%uKB min=%uKB underruns=%u net=%uKB/s\n",
                        (unsigned)(ring->fillLevel() / 1024),
                        (unsigned)(ring->minFill() == 0xFFFFFFFF ? 0 : ring->minFill() / 1024),
                        (unsigned)ring->underruns(),
                        (unsigned)(ring->bytesIn() / 1024 / 2));  // /2s window
          ring->statsReset();
        }
      }
    }
    vTaskDelay(1);  // yield (1 tick)
  }
}
