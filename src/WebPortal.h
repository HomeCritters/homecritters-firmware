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
  enum GameNav { NAV_NONE, NAV_START, NAV_BACK };

  void begin(Pet* pet, AudioPlayer* audio, StatusLed* led, FerretActor* ferret,
             Clock* clock, std::function<void(Action)> onAction);
  void handle();               // call every loop (when connected)
  void pushState();            // force an immediate state broadcast

  // --- phone game controller (Doodle Jump played from the phone) ---
  void setScreen(const char* name) { _screenName = name; }  // report current screen
  void setGameScore(int s) { _gameScore = s; }              // shown on the phone
  GameNav consumeGameNav();     // pending start/back request (clears it)
  float gameTargetXNorm();      // horizontal target [0..1] from the phone, -1 if none/stale

  void startConfigPortal();    // opens WiFiManager (non-blocking)
  void process();              // pump the portal while configuring
  void cancelConfig();         // abort configuration (Exit button)
  bool configuring() const { return _configuring; }
  const char* apName() const { return "Furao-Ball"; }

  bool connected() const;
  String ip() const;

 private:
  WebServer _server{80};
  WebSocketsServer _ws{81};
  WiFiManager _wm;
  Pet* _pet = nullptr;
  AudioPlayer* _audio = nullptr;
  StatusLed* _led = nullptr;
  FerretActor* _ferret = nullptr;
  Clock* _clock = nullptr;
  std::function<void(Action)> _onAction;
  bool _serverUp = false;
  bool _configuring = false;
  unsigned long _lastBroadcast = 0;

  // phone game controller state
  const char* _screenName = "pet";
  int _gameScore = 0;
  volatile float _gameTx = -1.0f;             // last target x [0..1] from the phone
  volatile unsigned long _gameTxMs = 0;       // when it arrived (for staleness)
  volatile GameNav _navReq = NAV_NONE;        // pending start/back request

  void startServer();
  void handleRoot();
  void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len);
  void applyCommand(const String& msg);
  void broadcastState();
  String stateJson() const;
};
