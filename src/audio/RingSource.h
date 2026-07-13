#pragma once
#include <AudioFileSource.h>
#include "StreamRing.h"

// ============================================================
// RingSource: thin AudioFileSource adapter over a StreamRing, so
// ESP8266Audio decoders consume the ring like a file.
//
// The reader task owns the HTTP connection; this adapter never
// touches the network - it only reads bytes the producer already
// banked.
// ============================================================
class RingSource : public AudioFileSource {
 public:
  // Underrun grace: how long read() waits for producer progress before
  // declaring the stream dead. Generous on purpose - real disconnects reach
  // us earlier via StreamRing::setEof(), so this only limits a silent,
  // stuck server. abortFlag interrupts the wait for responsive stops.
  static constexpr uint32_t READ_GRACE_MS = 15000;

  RingSource(StreamRing* ring, volatile bool* abortFlag)
      : _ring(ring), _abort(abortFlag) {}

  uint32_t read(void* data, uint32_t len) override {
    const uint32_t n = _ring->read(data, len, READ_GRACE_MS, _abort);
    _pos += n;
    return n;
  }

  // Forward-only "seek": read-and-discard from the ring. The WAV parser skips
  // header chunks (LIST/fact) with seek(n, SEEK_CUR) - without this, Music
  // Assistant's live WAV streams ("Native" output protocol, used by its
  // AirPlay receiver session) failed at ReadWAVInfo. Backward is impossible.
  bool seek(int32_t pos, int dir) override {
    uint32_t skip;
    if (dir == SEEK_CUR && pos >= 0) skip = (uint32_t)pos;
    else if (dir == SEEK_SET && (uint32_t)pos >= _pos) skip = (uint32_t)pos - _pos;
    else return false;
    uint8_t buf[64];
    while (skip > 0) {
      const uint32_t n =
          _ring->read(buf, min(skip, (uint32_t)sizeof(buf)), READ_GRACE_MS, _abort);
      if (n == 0) return false;  // EOF/abort while skipping
      _pos += n;
      skip -= n;
    }
    return true;
  }
  bool close() override { return true; }  // connection is the reader's problem
  bool isOpen() override { return true; }
  // "Infinite" length: AudioGeneratorFLAC's eof callback is
  // `getPos() >= getSize()`, so 0 made it declare EOF on the very first
  // frame. Real end-of-stream is read() returning 0 (ring drained + EOF).
  uint32_t getSize() override { return 0x7FFFFFFF; }
  uint32_t getPos() override { return _pos; }

 private:
  StreamRing* _ring;
  volatile bool* _abort;
  uint32_t _pos = 0;
};
