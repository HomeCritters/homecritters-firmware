#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <functional>
#include "Pet.h"
#include "AudioPlayer.h"
#include "StatusLed.h"
#include "FerretActor.h"
#include "Clock.h"
#include "HaPanel.h"
#include "audio/StreamRing.h"

class Renderer;  // for the /shot.bmp screenshot endpoint

// ============================================================
// WebPortal: WiFi connectivity + web portal (React) + WebSocket.
//   - begin(): reconnects to the saved network (non-blocking).
//   - startConfigPortal(): opens WiFiManager in NON-blocking mode
//     (screen/loop stay alive); process() pumps the portal and
//     cancelConfig() aborts it (the on-screen Exit button).
//   - handle(): brings the server up once connected, serves HTTP
//     and WebSocket, and pushes state (React never polls).
//
// The React app (port 80, gzipped in flash) talks over WebSocket
// (port 81): it receives state (JSON) and sends text commands
// ("feed"/"pat"/..., "name:X", "vol:N", "clock:on", ...).
// ============================================================

class WebPortal {
 public:
  // Phone game controller: navigation requested over WebSocket.
  enum GameNav { NAV_NONE, NAV_START, NAV_BALL, NAV_SIMON, NAV_BACK };

  void begin(Pet* pet, AudioPlayer* audio, StatusLed* led, FerretActor* ferret,
             Clock* clock, Renderer* renderer, HaPanel* ha,
             std::function<void(Action)> onAction);
  void handle();               // call every loop (when connected)
  // Request a state broadcast. Coalesced: handle() sends at most one every
  // few ms no matter how many requests pile up (spam-clicking the portal
  // must not turn into a burst of synchronous TCP writes).
  void pushState() { _dirty = true; }

  // --- phone game controller (Doodle Jump played from the phone) ---
  void setScreen(const char* name) { _screenName = name; }  // report current screen
  // Score shown on the phone. Only nudges a broadcast when it actually changes,
  // so calling this every game frame costs nothing.
  void setGameScore(int s) { if (s != _gameScore) { _gameScore = s; _dirty = true; } }
  void setBattery(int pct) { if (pct != _battery) { _battery = pct; _dirty = true; } }
  GameNav consumeGameNav();     // pending start/back request (clears it)
  float gameTargetXNorm();      // horizontal target [0..1] from the phone, -1 if none/stale
  // Pending Bolinha throw from the phone (normalized swipe). True once; fills
  // nx/ny in roughly [-1..1] (ny negative = upward).
  bool consumeBallThrow(float& nx, float& ny);
  // Pending Genius color press from the phone (0..3). True once.
  bool consumeSimonPress(int& color);

  // --- voice assistant (push-to-talk) ---
  // The device drives a voice turn: on BOOT hold it streams mic audio to the
  // subscribed voice client (HA sent "voice:sub") and pushes "evt:ptt:start";
  // on release it stops and pushes "evt:ptt:end". HA runs STT->TTS and plays
  // the reply back via "media:play:" (the existing TTS voice-ring path).
  void voicePttStart();
  void voicePttEnd();
  // Mic mute (privacy, Echo-style): while muted NO audio leaves the device -
  // PTT is denied and mic:on is ignored. Toggled by a quick BOOT tap, the
  // portal switch and HA ("mute:on|off"). Persisted in NVS.
  bool micMuted() const { return _micMuted; }
  void setMicMuted(bool muted);
  // Voice UI state, driven by the main loop's state machine (idle|listening|
  // thinking|speaking). Reflected in the WS state so the portal/HA can mirror
  // the on-screen ring. Coalesced: only nudges a broadcast on change.
  void setVoiceState(const char* s) {
    if (strcmp(_voiceState, s) != 0) { _voiceState = s; _dirty = true; }
  }
  const char* voiceState() const { return _voiceState; }

  // --- full sleep (night mode: screen + LED off, pet asleep) ---
  // Pending "fullsleep:on|off" request from HA/portal; -1 none, 0 off, 1 on.
  int consumeFullSleep() { const int v = _fullSleepReq; _fullSleepReq = -1; return v; }
  // Main reports the actual mode so the portal/HA switch mirror it.
  void setFullSleep(bool on) {
    if (on != _fullSleep) { _fullSleep = on; _dirty = true; }
  }
  // Night-mode sound settings ("sleepsnd:on|off" / "wakesnd:on|off"). Pending
  // change (-1 untouched); true when either arrived. Main applies + persists.
  bool consumeNightSnd(int& sleepSnd, int& wakeSnd) {
    sleepSnd = _sleepSndReq; wakeSnd = _wakeSndReq;
    _sleepSndReq = _wakeSndReq = -1;
    return sleepSnd >= 0 || wakeSnd >= 0;
  }
  void setNightSnd(bool sleepSnd, bool wakeSnd) {
    if (sleepSnd != _sleepSnd || wakeSnd != _wakeSnd) {
      _sleepSnd = sleepSnd; _wakeSnd = wakeSnd; _dirty = true;
    }
  }

  // --- pairing auth (F-Sec 1) + challenge-response (F-Sec 2) ---
  // Long-lived 16-hex credential in NVS - and it NEVER travels on the wire:
  // on connect the device sends "challenge:<nonce>" (fresh per socket) and
  // the client authenticates with "auth:<HMAC-SHA256(token, nonce)>[:cnonce]"
  // (hex). With the optional client nonce the device replies
  // "proof:<HMAC(token, cnonce)>" - MUTUAL auth: a spoofed device can't
  // impersonate the ball, and a spoofed one can't harvest anything reusable.
  // Until authed: no state, no commands, no mic (5s grace, then dropped).
  // /shot.bmp does its own dance (401+nonce -> ?sig=); /info stays public.
  // New clients get the token via PIN pairing:
  //   "pair:start" -> a random 6-digit PIN pops on the SCREEN (90s window)
  //   -> "pair:<pin>" -> device replies "token:<token>" (the one moment the
  //   token is on the wire: a short, user-supervised handover) and marks the
  //   socket authenticated. 3 wrong PINs or timeout close the window.
  // PER-CLIENT credentials (F-Sec 3): each PIN pairing mints its OWN token in
  // a slot (NVS table, up to MAX_CREDS). That makes revocation individual -
  // dropping one client's credential leaves the others untouched.
  const char* authToken() const { return _creds[0].token; }  // slot 0 (serial/test)
  void startPairing();  // opens (or extends) the PIN window - also the menu tile
  void cancelPairing();  // the X on the pairing overlay
  bool pairingActive() const { return _pairUntil != 0 && millis() < _pairUntil; }
  const char* pairingPin() const { return _pairPin; }
  // Revoke ALL credentials + kick everyone (hardware "Revogar acesso" button
  // and "revoke:all"). Every client must PIN-pair again.
  void revokeAll();
  // Revoke ONE credential slot (portal/HA "encerrar conexao"): wipes that
  // slot and kicks any socket using it; the others keep working.
  void revokeSlot(int slot);
  // --- HA panel (SCREEN_HA) ---
  // Toggle a device from the panel; request a fresh entity list on open.
  void sendHaCmd(const char* entityId, const char* action);
  void haSubscribe();  // ask the plugin to re-push ha:list

  // '\n'-separated IP list of connected clients (hardware Conexoes page).
  void clientsInfo(char* out, size_t n);
  // JSON array of connected clients for the portal/HA manager. Marks the
  // recipient's own row via its socket number `selfNum`.
  void clientsJson(char* out, size_t n, int selfNum);

  void startConfigPortal();    // opens WiFiManager (non-blocking; frees port 80)
  void process();              // pump the portal while configuring
  void cancelConfig();         // abort configuration (Exit button)
  bool configuring() const { return _configuring; }
  const char* apName() const { return "HomeCritters"; }

  bool connected() const;
  String ip() const;

 private:
  WebServer _server{80};
  WebSocketsServer _ws{81};
  WiFiManager _wm;
  Pet* _pet = nullptr;
  AudioPlayer* _audio = nullptr;
  StatusLed* _led = nullptr;
  Renderer* _renderer = nullptr;
  uint8_t* _bmp = nullptr;  // assembled BMP for /shot.bmp (lazy, PSRAM)
  FerretActor* _ferret = nullptr;
  Clock* _clock = nullptr;
  HaPanel* _ha = nullptr;   // HA control panel model (SCREEN_HA)
  std::function<void(Action)> _onAction;
  bool _serverUp = false;
  volatile bool _configuring = false;  // written on core 1, read by the core-0 http task
  bool _dirty = false;             // a state change is waiting to be broadcast
  unsigned long _lastBroadcast = 0;

  // phone game controller state
  const char* _screenName = "pet";
  int _gameScore = 0;
  int _battery = -1;
  volatile float _gameTx = -1.0f;             // last target x [0..1] from the phone
  volatile unsigned long _gameTxMs = 0;       // when it arrived (for staleness)
  volatile GameNav _navReq = NAV_NONE;        // pending start/back request
  volatile bool _throwReq = false;            // pending Bolinha throw
  volatile float _throwNx = 0, _throwNy = 0;  // normalized swipe of that throw
  volatile int _simonPress = -1;              // pending Genius color press

  // Mic capture -> HA (voice assistant). Raw 16kHz mono 16-bit PCM. Hardened
  // like ESPHome: a dedicated capture task (producer) fills a PSRAM ring off
  // the render loop, so I2S capture never waits on the network; handle()
  // (consumer, same thread as _ws.loop -> single-threaded WS) drains the ring
  // and sends only when the client can take it, dropping on backpressure so a
  // stalled client can never freeze rendering. Half-duplex: capture pauses
  // while audio plays (playback owns the shared I2S clock).
  volatile bool _micOn = false;  // written on render loop, read by capture task
  volatile bool _micMuted = false;  // privacy mute (NVS-persisted)
  int _micClient = -1;      // WS client num to stream audio to (HA voice sink)

  // Per-client credential table (NVS "auth", keys t<i>). Labels are NOT stored
  // here: they're per-connection (below), set by the client on each connect,
  // so two sockets sharing one credential still show distinct names.
  static constexpr int MAX_CREDS = 8;
  struct Cred { char token[17]; };  // token[0]==0 = empty slot
  Cred _creds[MAX_CREDS] = {};
  int _credCount() const;                // non-empty slots
  int _freeCredSlot();                   // first empty (evicts an idle one if full)
  void _saveCred(int slot);
  void _clearCred(int slot);
  bool _wsAuthed[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};
  int _wsSlot[WEBSOCKETS_SERVER_CLIENT_MAX];             // cred slot each socket used
  char _wsLabel[WEBSOCKETS_SERVER_CLIENT_MAX][25] = {};  // per-connection name
  bool _wsPairing[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};  // exempt from sweep
  unsigned long _wsConnAt[WEBSOCKETS_SERVER_CLIENT_MAX] = {0};  // 0 = free slot
  char _wsNonce[WEBSOCKETS_SERVER_CLIENT_MAX][33] = {};  // per-socket challenge
  bool _clientsDirty = false;    // push an updated clients: list next handle()
  char _pairPin[7] = {0};        // current 6-digit PIN ("" = none)
  unsigned long _pairUntil = 0;  // window deadline (0 = closed)
  uint8_t _pairAttempts = 0;     // wrong PINs this window (3 = close)
  int _pairSlot = -1;            // slot minted for the current pairing
  char _shotNonce[33] = {0};     // one-shot /shot.bmp challenge
  unsigned long _shotNonceAt = 0;
  void endPairing();
  void sendAuthedTXT(const char* msg);  // broadcast to authed clients only
  static void randHex(char* out, size_t hexChars);   // esp_random -> hex
  static void hmacHex(const char* key, const char* msg, char out65[65]);
  const char* _voiceState = "idle";  // idle|listening (device-side PTT feedback)
  volatile int _fullSleepReq = -1;   // pending fullsleep command (-1 none)
  bool _fullSleep = false;           // actual mode, reported by main
  volatile int _sleepSndReq = -1, _wakeSndReq = -1;  // pending sound settings
  bool _sleepSnd = true, _wakeSnd = true;            // reported by main
  StreamRing _micRing;      // producer = capture task, consumer = handle()
  static void micCaptureTask(void* arg);
  void micCaptureLoop();
  void pumpMic();           // drain the ring -> WS (called from handle())

  void startServer();
  void endConfig();  // leave config mode + reclaim port 80
  static void httpTask(void* arg);  // runs the HTTP server on core 0
  void handleRoot();
  void handleShot();  // GET /shot.bmp -> current screen as a BMP
  void handleInfo();  // GET /info -> identity JSON (HA config flow)
  void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len);
  void applyCommand(const String& msg);
  void broadcastState();
  // Builds the state JSON into a caller-provided buffer (no heap churn).
  void stateJson(char* out, size_t n) const;
};
