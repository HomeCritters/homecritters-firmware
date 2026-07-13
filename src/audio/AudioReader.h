#pragma once
#include <Arduino.h>
#include "StreamRing.h"

// ============================================================
// AudioReader: HTTP network reader task - the Voice PE "reader"
// stage, ported to Arduino. One persistent task on core 0 (with
// the WiFi stack), parked when idle, that pulls an http:// body
// into the StreamRing with zero-copy esp_http_client reads.
//
// Why esp_http_client (IDF) and not WiFiClient/HTTPClient:
//  - de-chunks Transfer-Encoding inside the SDK (the old raw path
//    fed chunk framing to the decoders as garbage bytes),
//  - follows redirects (media_source/TTS URLs),
//  - measured ~equal throughput to raw sockets (netbench nb2),
//  - opens the road to https later (esp-tls).
//
// Ownership: the reader task is the ONLY code touching the HTTP
// client; the decoder side only sees the ring. Stop protocol:
// stop() sets _abort -> task closes the client and parks ->
// caller waits for idle() before ring.reset().
// ============================================================
class AudioReader {
 public:
  // Create the parked task (core 0). Call once at boot.
  void begin(StreamRing* ring);

  // Start fetching url into the ring (ring must be reset by the caller
  // first). Returns false if the reader isn't idle, the url isn't http://,
  // or it doesn't fit. On any later failure the ring gets EOF - the
  // consumer drains and ends, no separate error path needed.
  bool start(const char* url);

  // Request stop (async). The task aborts the transfer, closes the client
  // and parks; poll idle() (worst case ~read timeout, <2s).
  void stop() { _abort = true; }

  bool idle() const { return _state == State::Idle; }
  // Content-Length of the current/last body (-1 = unknown/chunked).
  int64_t contentLength() const { return _contentLength; }

 private:
  enum class State : uint8_t { Idle, Running };

  static void taskTrampoline(void* arg);
  void taskLoop();
  void fetch();  // one transfer: open -> headers -> read loop -> close

  StreamRing* _ring = nullptr;
  TaskHandle_t _task = nullptr;
  volatile State _state = State::Idle;
  volatile bool _abort = false;
  volatile int64_t _contentLength = -1;
  char _url[224] = {0};
};
