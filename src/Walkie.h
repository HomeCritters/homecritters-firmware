#pragma once
#include <Arduino.h>
#include <AsyncUDP.h>
#include "audio/StreamRing.h"

class AudioPlayer;
class WebPortal;

// ============================================================
// Walkie: Apple Watch-style walkie-talkie between HomeCritters on the LAN.
//
// Discovery: mDNS _critter._tcp (each device advertises TXT mac/name/wt); a
// one-shot scan task browses it and fills the peer table. Trust: any valid
// packet on the LAN plays (the home network is the perimeter - owner's call).
//
// Transport: UDP port 40890, 16-byte header + 640B PCM16 mono 16kHz frames
// (20ms each, ~33KB/s). Unicast to a friend, subnet broadcast for "Todos".
// Release sends 3x WT_TALK_END (loss insurance); receivers also time out.
//
// Audio: TX reuses the mic capture ring (WebPortal::walkieMicStart claims the
// mic; pumpTx on the render loop drains it to UDP). RX lands in a 16KB PSRAM
// ring written by the lwIP task (StreamRing is SPSC: writer=onPacket,
// reader=audio task) and is drained by AudioPlayer's decoder task through
// AudioCodec::playRaw16 - both sides run at 16kHz so the shared I2S clock
// never retunes. Half-duplex falls out of AudioPlayer::busy() including the
// RX session: the mic task yields while anything plays. No AEC needed.
// ============================================================

static constexpr uint16_t WT_PORT = 40890;
static constexpr uint32_t WT_MAGIC = 0x54574348;  // "HCWT" little-endian

enum WtPktType : uint8_t { WT_AUDIO = 1, WT_AUDIO_BCAST = 2, WT_TALK_END = 3 };

struct __attribute__((packed)) WtHdr {
  uint32_t magic;
  uint8_t ver;     // 1
  uint8_t type;    // WtPktType
  uint16_t seq;    // frame counter within a talk session
  uint8_t mac[6];  // sender identity (STA MAC)
  uint16_t len;    // payload bytes: 640 audio, 0 talk-end
};
static constexpr size_t WT_FRAME = 640;  // 320 samples / 20ms @ 16kHz mono

struct WtPeer {
  char name[19];  // pet name from the TXT record
  uint8_t mac[6];
  IPAddress ip;
  unsigned long lastSeenMs;
};

enum WtState : uint8_t { WT_IDLE, WT_TX, WT_RX };

class Walkie {
 public:
  static constexpr int MAX_PEERS = 8;

  void begin(AudioPlayer* audio, WebPortal* web);
  void setEnabled(bool on);   // NVS + TXT wt= + drops RX when off
  bool enabled() const { return _enabled; }
  void setFullSleep(bool on) { _fullSleep = on; }

  // Peer discovery (browse runs on a throwaway task; results flip in whole).
  void requestScan();
  bool scanning() const { return _scanning; }
  int peerCount() const { return _peerCount; }
  const WtPeer& peer(int i) const { return _peers[i]; }

  // Talk session. peerIdx -1 = broadcast ("Todos").
  bool txStart(int peerIdx);
  void txEnd();
  WtState state() const { return _state; }
  const char* rxName() const { return _rxName; }  // who we're hearing
  int txTarget() const { return _txTarget; }
  // Peer-table index of the current/last RX sender (-1 unknown): the UI
  // auto-opens the talk screen pointed at whoever called.
  int rxPeerIndex() const { return _rxPeerIdx; }

  // Render-loop pumps (same cadence family as pumpMic/pumpScreen).
  void pumpTx();
  void tick(unsigned long now);  // timeouts, WiFi power, RX session reaping

  void printStats() const;  // serial `wt`
  // Compact stats JSON for the portal/WS (validation + dev panel).
  void statsJson(char* out, size_t n) const;

  // RX ring accessors for AudioPlayer's drain (audio task).
  StreamRing& rxRing() { return _rxRing; }

 private:
  static void scanTask(void* arg);
  void onPacket(AsyncUDPPacket& p);
  void endRxSession();

  AudioPlayer* _audio = nullptr;
  WebPortal* _web = nullptr;
  AsyncUDP _udp;
  StreamRing _rxRing;

  volatile bool _enabled = true;
  volatile bool _fullSleep = false;
  volatile WtState _state = WT_IDLE;

  // Peer table: staging filled by the scan task, flipped under the mux.
  WtPeer _peers[MAX_PEERS] = {};
  volatile int _peerCount = 0;
  volatile bool _scanning = false;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  // TX session
  int _txTarget = -1;           // peer index, -1 broadcast
  IPAddress _txIp;
  uint16_t _txSeq = 0;
  unsigned long _txStartMs = 0;

  // RX session (written by the lwIP task, read by loop/audio - volatiles)
  uint8_t _rxMac[6] = {0};      // session lock: first sender wins
  char _rxName[19] = {0};
  volatile unsigned long _rxLastMs = 0;
  volatile unsigned long _rxStartMs = 0;
  volatile int _rxPeerIdx = -1;  // sender's slot in the peer table
  unsigned long _rxWindowMs = 0;  // rate limit window
  uint8_t _rxWindowPkts = 0;

  uint8_t _selfMac[6] = {0};

  // Observability (serial `wt`)
  uint32_t _txPkts = 0, _txBytes = 0;
  volatile uint32_t _rxPkts = 0, _rxDropTx = 0, _rxDropOther = 0;
  volatile uint32_t _rxDropBusy = 0, _rxDropBad = 0, _rxDropOff = 0;
  volatile uint16_t _rxLastSeq = 0;
};
