#pragma once
#include <Arduino.h>

// ============================================================
// StreamRing: SPSC byte ring in PSRAM between the network reader
// task (producer) and the decoder task (consumer). The Voice PE
// "raw file ring buffer" equivalent.
//
// Lock-free by construction: producer writes _w, consumer writes
// _r; both are 32-bit (atomic on ESP32) absolute byte counters
// (index = counter % cap) whose difference wraps cleanly.
//
// Lifecycle: alloc() ONCE at boot, never freed; reset() between
// tracks - but ONLY when both sides are quiescent (reader parked,
// decoder deleted). A permanent ring eliminates by construction
// the producer-vs-cleanup use-after-free the old design had.
// ============================================================
class StreamRing {
 public:
  bool alloc(uint32_t cap);  // PSRAM (heap fallback); false if OOM

  // ---- producer side (reader task ONLY) ----
  // Contiguous free span at the write position for a zero-copy read from the
  // network straight into the ring. nullptr when full (contig set to 0).
  uint8_t* writeRegion(uint32_t& contig);
  void commit(uint32_t n);  // after writing n bytes into writeRegion()
  void setEof();            // clean end OR fatal reader error: consumer drains + stops

  // ---- consumer side (decoder task ONLY) ----
  // Blocks until the FULL len is copied (like a file source - partial reads
  // made AudioGeneratorWAV misparse the RIFF header: 1/4-speed playback).
  // Returns fewer only on: EOF (drained), *abort set, or graceMs with no
  // producer progress (silent stuck server).
  uint32_t read(void* dst, uint32_t len, uint32_t graceMs, volatile bool* abort);
  // Copy up to n buffered bytes WITHOUT consuming (header sniff for decoder
  // selection). Consumer-side only.
  uint32_t peek(uint8_t* out, uint32_t n) const;

  uint32_t fill() const { return (uint32_t)(_w - _r); }
  uint32_t capacity() const { return _cap; }
  bool eof() const { return _eof; }

  // Both sides quiescent ONLY.
  void reset();

  // Rolling stats for profiling; statsReset() clears the window.
  uint32_t underruns() const { return _underruns; }
  uint32_t minFill() const { return _minFill == 0xFFFFFFFF ? 0 : _minFill; }
  uint32_t bytesIn() const { return _bytesIn; }
  void statsReset() {
    _underruns = 0;
    _minFill = 0xFFFFFFFF;
    _bytesIn = 0;
  }

 private:
  uint8_t* _buf = nullptr;
  uint32_t _cap = 0;
  volatile uint32_t _w = 0, _r = 0;  // absolute byte counters
  volatile bool _eof = false;
  uint32_t _underruns = 0, _minFill = 0xFFFFFFFF, _bytesIn = 0;
};
