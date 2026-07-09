#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <functional>
#include "Pet.h"
#include "AudioPlayer.h"
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
  void begin(Pet* pet, AudioPlayer* audio, FerretActor* ferret, Clock* clock,
             std::function<void(Action)> onAction);
  void handle();               // call every loop (when connected)
  void pushState();            // force an immediate state broadcast

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
  FerretActor* _ferret = nullptr;
  Clock* _clock = nullptr;
  std::function<void(Action)> _onAction;
  bool _serverUp = false;
  bool _configuring = false;
  unsigned long _lastBroadcast = 0;

  void startServer();
  void handleRoot();
  void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len);
  void applyCommand(const String& msg);
  void broadcastState();
  String stateJson() const;
};
