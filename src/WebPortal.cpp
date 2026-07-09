#include "WebPortal.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <cstring>
#include "web_index.h"  // gzipped single-file React portal

static constexpr const char* HOSTNAME = "ferret";  // -> ferret.local

void WebPortal::begin(Pet* pet, AudioPlayer* audio, FerretActor* ferret, Clock* clock,
                      std::function<void(Action)> onAction) {
  _pet = pet;
  _audio = audio;
  _ferret = ferret;
  _clock = clock;
  _onAction = onAction;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin();  // reconnect using previously saved credentials
}

bool WebPortal::connected() const { return WiFi.status() == WL_CONNECTED; }
String WebPortal::ip() const { return WiFi.localIP().toString(); }

// ---- WiFi setup (non-blocking captive portal) ----
void WebPortal::startConfigPortal() {
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(180);
  _wm.startConfigPortal(apName());
  _configuring = true;
}

void WebPortal::process() {
  if (!_configuring) return;
  _wm.process();
  if (!_wm.getConfigPortalActive()) _configuring = false;  // connected or timed out
}

void WebPortal::cancelConfig() {
  _wm.stopConfigPortal();
  _configuring = false;
}

// ---- HTTP + WebSocket servers ----
void WebPortal::handle() {
  if (!_serverUp && connected()) startServer();
  if (!_serverUp) return;
  _server.handleClient();
  _ws.loop();
  // While walking, push position ~10x/s (keeps the portal ~1:1 with the
  // hardware); when still, ~2x/s is plenty for the stats.
  unsigned long now = millis();
  bool walking = _ferret && strcmp(_ferret->animName(), "walk") == 0;
  unsigned long interval = walking ? 100 : 500;
  if (now - _lastBroadcast > interval) {
    broadcastState();
    _lastBroadcast = now;
  }
}

void WebPortal::startServer() {
  _server.on("/", [this]() { handleRoot(); });
  _server.onNotFound([this]() { handleRoot(); });  // SPA fallback
  _server.begin();
  _ws.begin();
  _ws.onEvent([this](uint8_t n, WStype_t t, uint8_t* p, size_t l) { onWsEvent(n, t, p, l); });
  // mDNS: friendly access via ferret.local (Bonjour/Avahi)
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }
  _serverUp = true;
  Serial.printf("[web] portal at http://%s.local/  (ip %s, ws :81)\n", HOSTNAME, ip().c_str());
}

// Serve the React portal (single file) straight from flash, gzipped.
void WebPortal::handleRoot() {
  _server.sendHeader("Content-Encoding", "gzip");
  _server.sendHeader("Cache-Control", "no-cache");  // always fetch the current build
  _server.send_P(200, "text/html", (const char*)web_index_gz, web_index_gz_len);
}

void WebPortal::onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    String s = stateJson();
    _ws.sendTXT(num, s);  // send the current state on connect
  } else if (type == WStype_TEXT) {
    String msg;
    msg.reserve(len);
    for (size_t i = 0; i < len; i++) msg += (char)payload[i];
    applyCommand(msg);
    broadcastState();  // reflect immediately to every client
  }
}

// Commands from React: "feed"/"pat"/"clean"/"sleep", "name:NewName",
// "vol:N", "clock:on|off", "fmt:12|24", "tz:<posix>", "idle:<sec>".
void WebPortal::applyCommand(const String& msg) {
  if (msg.startsWith("name:")) {
    if (_pet) _pet->setName(msg.substring(5));
    return;
  }
  if (msg.startsWith("vol:")) {
    if (_audio) _audio->setVolume(msg.substring(4).toInt());
    return;
  }
  if (_clock) {
    if (msg == "clock:on")  { _clock->setEnabled(true);  return; }
    if (msg == "clock:off") { _clock->setEnabled(false); return; }
    if (msg == "fmt:24")    { _clock->setH24(true);  return; }
    if (msg == "fmt:12")    { _clock->setH24(false); return; }
    if (msg.startsWith("tz:"))   { _clock->setTz(msg.substring(3)); return; }
    if (msg.startsWith("idle:")) { _clock->setIdleSec(msg.substring(5).toInt()); return; }
  }
  if (!_onAction) return;
  if (msg == "feed")       _onAction(ACTION_FEED);
  else if (msg == "pat")   _onAction(ACTION_PAT);
  else if (msg == "clean") _onAction(ACTION_CLEAN);
  else if (msg == "sleep") _onAction(ACTION_TOGGLE_SLEEP);
}

void WebPortal::pushState() { broadcastState(); }

void WebPortal::broadcastState() {
  if (!_serverUp) return;
  String s = stateJson();
  _ws.broadcastTXT(s);
}

String WebPortal::stateJson() const {
  const Pet& p = *_pet;
  String j = "{";
  j += "\"name\":\"" + p.name() + "\",";
  j += "\"sleeping\":" + String(p.sleeping() ? "true" : "false") + ",";
  j += "\"volume\":" + String(_audio ? _audio->volume() : 0) + ",";
  j += "\"clockOn\":" + String(_clock && _clock->enabled() ? "true" : "false") + ",";
  j += "\"tz\":\"" + String(_clock ? _clock->tz() : String("")) + "\",";
  j += "\"idleSec\":" + String(_clock ? _clock->idleSec() : 30) + ",";
  j += "\"h24\":" + String(_clock && _clock->h24() ? "true" : "false") + ",";
  j += "\"anim\":\"" + String(_ferret ? _ferret->animName() : "idle") + "\",";
  j += "\"seq\":" + String(_ferret ? _ferret->animSeq() : 0) + ",";
  j += "\"flip\":" + String(_ferret && _ferret->faceLeft() ? "true" : "false") + ",";
  j += "\"x\":" + String(_ferret ? _ferret->xNorm() : 0.5f, 3) + ",";
  j += "\"hunger\":" + String(p.hunger(), 1) + ",";
  j += "\"energy\":" + String(p.energy(), 1) + ",";
  j += "\"joy\":" + String(p.joy(), 1) + ",";
  j += "\"hygiene\":" + String(p.hygiene(), 1) + "}";
  return j;
}
