#include "StreamRing.h"

bool StreamRing::alloc(uint32_t cap) {
  if (_buf) return true;  // once
  _buf = (uint8_t*)ps_malloc(cap);
  if (!_buf) _buf = (uint8_t*)malloc(cap);  // tiny-heap fallback (tests)
  if (!_buf) return false;
  _cap = cap;
  reset();
  return true;
}

uint8_t* StreamRing::writeRegion(uint32_t& contig) {
  const uint32_t used = (uint32_t)(_w - _r);
  const uint32_t space = _cap - used;
  if (space == 0) {
    contig = 0;
    return nullptr;  // full
  }
  const uint32_t at = _w % _cap;
  contig = min(space, _cap - at);  // stop at the wrap point
  return _buf + at;
}

void StreamRing::commit(uint32_t n) {
  _w += n;
  _bytesIn += n;
}

void StreamRing::setEof() { _eof = true; }

uint32_t StreamRing::write(const void* src, uint32_t len) {
  uint32_t done = 0;
  while (done < len) {
    uint32_t contig;
    uint8_t* dst = writeRegion(contig);
    if (!dst || contig == 0) break;  // full: drop the rest (newest)
    const uint32_t n = min(contig, len - done);
    memcpy(dst, (const uint8_t*)src + done, n);
    commit(n);
    done += n;
  }
  return done;
}

uint32_t StreamRing::readAvail(void* dst, uint32_t maxLen) {
  const uint32_t avail = (uint32_t)(_w - _r);
  uint32_t n = min(avail, maxLen);
  uint32_t done = 0;
  while (done < n) {
    const uint32_t at = _r % _cap;
    const uint32_t chunk = min(n - done, _cap - at);
    memcpy((uint8_t*)dst + done, _buf + at, chunk);
    _r += chunk;
    done += chunk;
  }
  return done;
}

uint32_t StreamRing::read(void* dst, uint32_t len, uint32_t graceMs,
                          volatile bool* abort) {
  uint32_t got = 0;
  uint32_t t0 = millis();
  bool waited = false;
  while (got < len) {
    const uint32_t avail = (uint32_t)(_w - _r);
    if (avail > 0) {
      if (waited) {
        _underruns++;
        waited = false;
      }
      if (avail < _minFill) _minFill = avail;
      const uint32_t want = min(avail, len - got);
      const uint32_t at = _r % _cap;
      const uint32_t first = min(want, _cap - at);
      memcpy((uint8_t*)dst + got, _buf + at, first);
      if (want > first) memcpy((uint8_t*)dst + got + first, _buf, want - first);
      _r += want;
      got += want;
      t0 = millis();  // progress: reset the grace window
      continue;
    }
    if (_eof) break;                  // producer finished and we drained it
    if (abort && *abort) break;       // stop requested: return what we have
    if (millis() - t0 > graceMs) break;  // silent stuck producer
    waited = true;
    vTaskDelay(1);  // wait for the producer to deliver more bytes
  }
  return got;
}

uint32_t StreamRing::peek(uint8_t* out, uint32_t n) const {
  const uint32_t avail = (uint32_t)(_w - _r);
  n = min(n, avail);
  const uint32_t at = _r % _cap;
  const uint32_t first = min(n, _cap - at);
  memcpy(out, _buf + at, first);
  if (n > first) memcpy(out + first, _buf, n - first);
  return n;
}

void StreamRing::reset() {
  _w = 0;
  _r = 0;
  _eof = false;
  statsReset();
}
