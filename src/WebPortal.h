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
             Clock* clock, Renderer* renderer, std::function<void(Action)> onAction);
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
  const char* voiceState() const { return _voiceState; }

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
  int _micClient = -1;      // WS client num to stream audio to (HA voice sink)
  const char* _voiceState = "idle";  // idle|listening (device-side PTT feedback)
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
