#include "Walkie.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "AudioPlayer.h"
#include "TaskRegistry.h"
#include "WebPortal.h"

// Talk sessions: RX releases after this long without a packet (the sender
// also announces the end with 3x WT_TALK_END, this is the loss fallback).
static constexpr unsigned long RX_TIMEOUT_MS = 400;
static constexpr unsigned long RX_MAX_MS = 60000;  // stuck-sender guard
static constexpr unsigned long TX_MAX_MS = 60000;  // stuck-finger guard

void Walkie::begin(AudioPlayer* audio, WebPortal* web) {
  _audio = audio;
  _web = web;
  WiFi.macAddress(_selfMac);

  Preferences p;
  p.begin("walkie", true);
  _enabled = p.getBool("en", true);
  p.end();

  _rxRing.alloc(16 * 1024);  // ~0.5s of 16k mono PCM (PSRAM)
  _audio->walkieRxAttach(&_rxRing);

  if (_udp.listen(WT_PORT)) {
    _udp.onPacket([this](AsyncUDPPacket p) { onPacket(p); });
    Serial.printf("[wt] listening on udp:%u\n", WT_PORT);
  } else {
    Serial.println("[wt] udp listen FAILED");
  }
}

void Walkie::setEnabled(bool on) {
  if (on == _enabled) return;
  _enabled = on;
  Preferences p;
  p.begin("walkie", false);
  p.putBool("en", on);
  p.end();
  MDNS.addServiceTxt("critter", "tcp", "wt", on ? "1" : "0");  // replaces
  if (!on && _state == WT_RX) endRxSession();
}

// ---- discovery -------------------------------------------------------------
// MDNS.queryService blocks ~3s, so it runs on a throwaway core-0 task (the
// topTask pattern). Results build in a staging array and flip in whole under
// the mux; the UI polls peerCount()/peer().
void Walkie::scanTask(void* arg) {
  Walkie* self = static_cast<Walkie*>(arg);
  WtPeer staging[MAX_PEERS] = {};
  int n = 0;
  const int found = MDNS.queryService("critter", "tcp");
  for (int i = 0; i < found && n < MAX_PEERS; i++) {
    if (!MDNS.hasTxt(i, "mac")) continue;
    if (MDNS.hasTxt(i, "wt") && MDNS.txt(i, "wt") == "0") continue;  // walkie off
    uint8_t mac[6] = {0};
    sscanf(MDNS.txt(i, "mac").c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (memcmp(mac, self->_selfMac, 6) == 0) continue;  // that's us
    WtPeer& pr = staging[n];
    String nm = MDNS.hasTxt(i, "name") ? MDNS.txt(i, "name") : MDNS.hostname(i);
    strlcpy(pr.name, nm.c_str(), sizeof(pr.name));
    memcpy(pr.mac, mac, 6);
    pr.ip = MDNS.IP(i);
    pr.lastSeenMs = millis();
    n++;
  }
  portENTER_CRITICAL(&self->_mux);
  memcpy(self->_peers, staging, sizeof(staging));
  self->_peerCount = n;
  portEXIT_CRITICAL(&self->_mux);
  self->_scanning = false;
  Serial.printf("[wt] scan: %d peer(s)\n", n);
  vTaskDelete(nullptr);
}

void Walkie::requestScan() {
  if (_scanning) return;  // single-flight
  _scanning = true;
  xTaskCreatePinnedToCore(scanTask, "wtscan", 4096, this, 1, nullptr, 0);
}

// ---- RX (lwIP task context: validate, gate, stash - nothing heavy) ---------
void Walkie::onPacket(AsyncUDPPacket& p) {
  if (p.length() < sizeof(WtHdr)) { _rxDropBad++; return; }
  const WtHdr* h = (const WtHdr*)p.data();
  if (h->magic != WT_MAGIC || h->ver != 1) { _rxDropBad++; return; }
  if (p.length() != sizeof(WtHdr) + h->len) { _rxDropBad++; return; }
  if (memcmp(h->mac, _selfMac, 6) == 0) return;  // our own broadcast echo

  if (!_enabled || _fullSleep) { _rxDropOff++; return; }
  if (_state == WT_TX) { _rxDropTx++; return; }           // half-duplex: TX wins
  if (_audio && _audio->streaming()) { _rxDropBusy++; return; }  // media wins

  const unsigned long now = millis();
  if (_state == WT_RX) {
    if (memcmp(h->mac, _rxMac, 6) != 0) { _rxDropOther++; return; }  // session lock
  }

  if (h->type == WT_TALK_END) {
    if (_state == WT_RX) _rxLastMs = now - RX_TIMEOUT_MS;  // reap on next tick
    return;
  }
  if (h->type != WT_AUDIO && h->type != WT_AUDIO_BCAST) { _rxDropBad++; return; }
  if (h->len != WT_FRAME) { _rxDropBad++; return; }

  // Rate limit: max 60 audio pkts per rolling second per session.
  if (now - _rxWindowMs > 1000) { _rxWindowMs = now; _rxWindowPkts = 0; }
  if (++_rxWindowPkts > 60) { _rxDropBad++; return; }

  if (_state == WT_IDLE) {
    // New session: lock to this sender, resolve the name from the peer table
    // (fall back to the MAC tail), radio hot, chirp, start the audio drain.
    memcpy(_rxMac, h->mac, 6);
    snprintf(_rxName, sizeof(_rxName), "critter-%02x%02x%02x",
             h->mac[3], h->mac[4], h->mac[5]);
    int idx = -1;
    portENTER_CRITICAL(&_mux);
    for (int i = 0; i < _peerCount; i++) {
      if (memcmp(_peers[i].mac, h->mac, 6) == 0) {
        strlcpy(_rxName, _peers[i].name, sizeof(_rxName));
        _peers[i].lastSeenMs = now;  // passive liveness refresh
        _peers[i].ip = p.remoteIP();  // and the address (DHCP may move it)
        idx = i;
        break;
      }
    }
    if (idx < 0 && _peerCount < MAX_PEERS) {
      // Unknown caller: learn them from the packet (MAC + source IP) so the
      // auto-opened talk screen can reply unicast; a scan refines the name.
      WtPeer& pr = _peers[_peerCount];
      strlcpy(pr.name, _rxName, sizeof(pr.name));
      memcpy(pr.mac, h->mac, 6);
      pr.ip = p.remoteIP();
      pr.lastSeenMs = now;
      idx = _peerCount;
      _peerCount = _peerCount + 1;
    }
    portEXIT_CRITICAL(&_mux);
    _rxPeerIdx = idx;
    _rxRing.reset();
    _rxStartMs = now;
    _state = WT_RX;
    WiFi.setSleep(false);        // DTIM stalls would wreck the jitter ring
    _audio->walkieRxStart();     // chirp + raw drain in the audio task
  }
  _rxRing.write((const uint8_t*)p.data() + sizeof(WtHdr), h->len);
  _rxPkts++;
  _rxLastSeq = h->seq;
  _rxLastMs = now;
}

void Walkie::endRxSession() {
  _state = WT_IDLE;
  if (_audio) _audio->walkieRxStop();
  _rxRing.reset();
  memset(_rxMac, 0, sizeof(_rxMac));
  // Restore the radio policy (media/screen keep it hot on their own).
  WiFi.setSleep(!(_audio && _audio->streaming()));
}

// ---- TX --------------------------------------------------------------------
bool Walkie::txStart(int peerIdx) {
  if (!_enabled || _fullSleep) return false;
  // Radio etiquette: you can NOT talk over an incoming PTT - wait for the
  // other side to finish (the button buzzes, owner's rule).
  if (_state == WT_RX) return false;
  if (_web && !_web->walkieMicStart()) return false;  // HA voice PTT owns the mic
  if (peerIdx >= 0) {
    if (peerIdx >= _peerCount) { if (_web) _web->walkieMicStop(); return false; }
    _txIp = _peers[peerIdx].ip;
  }
  _txTarget = peerIdx;
  _txSeq = 0;
  _txStartMs = millis();
  _state = WT_TX;
  WiFi.setSleep(false);
  if (_audio) _audio->playWalkieChirp();  // capture starts after (busy gate)
  return true;
}

void Walkie::txEnd() {
  if (_state != WT_TX) return;
  // 3x TALK_END so the far side releases instantly even with UDP loss.
  uint8_t pkt[sizeof(WtHdr)];
  WtHdr* h = (WtHdr*)pkt;
  h->magic = WT_MAGIC; h->ver = 1; h->type = WT_TALK_END;
  h->seq = _txSeq; h->len = 0;
  memcpy(h->mac, _selfMac, 6);
  for (int i = 0; i < 3; i++) {
    if (_txTarget < 0) _udp.broadcastTo(pkt, sizeof(pkt), WT_PORT);
    else _udp.writeTo(pkt, sizeof(pkt), _txIp, WT_PORT);
  }
  _state = WT_IDLE;
  _txTarget = -1;
  if (_web) _web->walkieMicStop();
  if (_audio) _audio->playConfirm();  // roger beep
  WiFi.setSleep(!(_audio && _audio->streaming()));
}

// Render loop: drain mic frames to UDP (pumpMic cadence, max 4 per pass).
void Walkie::pumpTx() {
  if (_state != WT_TX || !_web) return;
  uint8_t pkt[sizeof(WtHdr) + WT_FRAME];
  WtHdr* h = (WtHdr*)pkt;
  for (int i = 0; i < 4; i++) {
    if (_web->micRing().fill() < WT_FRAME) break;
    _web->micRing().readAvail(pkt + sizeof(WtHdr), WT_FRAME);
    h->magic = WT_MAGIC; h->ver = 1;
    h->type = _txTarget < 0 ? WT_AUDIO_BCAST : WT_AUDIO;
    h->seq = _txSeq++; h->len = WT_FRAME;
    memcpy(h->mac, _selfMac, 6);
    const size_t sent = _txTarget < 0
        ? _udp.broadcastTo(pkt, sizeof(pkt), WT_PORT)
        : _udp.writeTo(pkt, sizeof(pkt), _txIp, WT_PORT);
    if (!sent) break;  // stack backpressure: retry next pass
    _txPkts++;
    _txBytes += sizeof(pkt);
  }
}

// Render loop: session timeouts + radio keepalive.
void Walkie::tick(unsigned long now) {
  if (_state == WT_RX) {
    if (now - _rxLastMs > RX_TIMEOUT_MS || now - _rxStartMs > RX_MAX_MS)
      endRxSession();
    else {
      static unsigned long lastPs = 0;  // keep the radio hot during the session
      if (now - lastPs > 1000) { lastPs = now; WiFi.setSleep(false); }
    }
  } else if (_state == WT_TX) {
    if (now - _txStartMs > TX_MAX_MS) txEnd();  // stuck finger / stuck injector
    else {
      static unsigned long lastPs = 0;
      if (now - lastPs > 1000) { lastPs = now; WiFi.setSleep(false); }
    }
  }
}

void Walkie::statsJson(char* out, size_t n) const {
  int o = snprintf(out, n,
                   "{\"en\":%d,\"state\":\"%s\",\"rxName\":\"%s\","
                   "\"txPkts\":%lu,\"rxPkts\":%lu,\"dropBad\":%lu,"
                   "\"dropOff\":%lu,\"dropTx\":%lu,\"dropOther\":%lu,"
                   "\"dropBusy\":%lu,\"lastSeq\":%u,\"peers\":[",
                   (int)_enabled,
                   _state == WT_TX ? "tx" : _state == WT_RX ? "rx" : "idle",
                   _rxName,
                   (unsigned long)_txPkts, (unsigned long)_rxPkts,
                   (unsigned long)_rxDropBad, (unsigned long)_rxDropOff,
                   (unsigned long)_rxDropTx, (unsigned long)_rxDropOther,
                   (unsigned long)_rxDropBusy, _rxLastSeq);
  for (int i = 0; i < _peerCount && o < (int)n - 2; i++)
    o += snprintf(out + o, n - o, "%s{\"name\":\"%s\",\"ip\":\"%s\"}",
                  i ? "," : "", _peers[i].name, _peers[i].ip.toString().c_str());
  snprintf(out + o, n - o, "]}");
}

void Walkie::printStats() const {
  Serial.printf("[wt] %s state=%s peers=%d scanning=%d\n",
                _enabled ? "on" : "off",
                _state == WT_TX ? "TX" : _state == WT_RX ? "RX" : "idle",
                _peerCount, (int)_scanning);
  Serial.printf("[wt] tx: %lu pkts %lu bytes | rx: %lu pkts lastSeq=%u\n",
                (unsigned long)_txPkts, (unsigned long)_txBytes,
                (unsigned long)_rxPkts, _rxLastSeq);
  Serial.printf("[wt] drops: bad=%lu off=%lu tx=%lu other=%lu busy=%lu\n",
                (unsigned long)_rxDropBad, (unsigned long)_rxDropOff,
                (unsigned long)_rxDropTx, (unsigned long)_rxDropOther,
                (unsigned long)_rxDropBusy);
  for (int i = 0; i < _peerCount; i++)
    Serial.printf("[wt]  peer %d: %s  %s  %02x:%02x:%02x:%02x:%02x:%02x\n", i,
                  _peers[i].name, _peers[i].ip.toString().c_str(),
                  _peers[i].mac[0], _peers[i].mac[1], _peers[i].mac[2],
                  _peers[i].mac[3], _peers[i].mac[4], _peers[i].mac[5]);
}
