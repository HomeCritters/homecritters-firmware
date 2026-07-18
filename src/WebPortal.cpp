#include "WebPortal.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <mbedtls/md.h>  // HMAC-SHA256 (challenge-response auth)
#include <cstring>
#include "Renderer.h"
#include "audio/AudioCodec.h"  // mic capture (readMicMono / setCaptureRate)
#include "web_index.h"  // gzipped single-file React portal

static constexpr const char* HOSTNAME = "critter";  // -> critter.local
static constexpr const char* FW_VERSION = "1.0.0";

static void jsonEscape(const String& in, char* out, size_t n);  // defined below

void WebPortal::begin(Pet* pet, AudioPlayer* audio, StatusLed* led, FerretActor* ferret,
                      Clock* clock, Renderer* renderer, HaPanel* ha,
                      std::function<void(Action)> onAction) {
  _pet = pet;
  _audio = audio;
  _led = led;
  _renderer = renderer;
  _ferret = ferret;
  _clock = clock;
  _ha = ha;
  _onAction = onAction;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  // Modem power save ON by default: the radio naps between AP beacons, which
  // cuts ~50mA and real heat (the S3 ran hot with PS off 24/7). The old
  // "always on" rule here predates the MCLK/GPIO0 fix - the latency spikes
  // blamed on modem sleep were really EMI desense. AudioPlayer escalates to
  // full radio power while a media stream plays and restores sleep after.
  WiFi.setSleep(true);
  WiFi.begin();  // reconnect using previously saved credentials

  Preferences prefs;
  prefs.begin("voice", true);
  _micMuted = prefs.getBool("muted", false);
  prefs.end();

  // Per-client credential table (NVS). Migrate a legacy single "token" into
  // slot 0 (so an existing HA/portal keeps working after the upgrade); slot 0
  // seeds fresh on a brand-new device too.
  for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) _wsSlot[i] = -1;
  prefs.begin("auth", false);
  for (int i = 0; i < MAX_CREDS; i++) {
    char k[4];
    snprintf(k, sizeof(k), "t%d", i);
    prefs.getString(k, _creds[i].token, sizeof(_creds[i].token));
  }
  // Migrate a legacy single "token" into slot 0 (keeps an existing HA/portal
  // working). A fresh or fully-revoked device just stays empty: clients pair
  // by PIN and get their own minted token - no unknown token to seed.
  bool migrated = false;
  if (_credCount() == 0) {
    String legacy = prefs.getString("token", "");
    if (legacy.length() == 16) {
      strlcpy(_creds[0].token, legacy.c_str(), 17);
      migrated = true;
    }
  }
  prefs.remove("token");  // legacy key retired (would resurrect after revoke)
  prefs.end();
  if (migrated) _saveCred(0);
}

int WebPortal::_credCount() const {
  int n = 0;
  for (int i = 0; i < MAX_CREDS; i++) if (_creds[i].token[0]) n++;
  return n;
}

int WebPortal::_freeCredSlot() {
  for (int i = 0; i < MAX_CREDS; i++) if (!_creds[i].token[0]) return i;
  // Table full: evict a slot that has no live connection.
  for (int i = 1; i < MAX_CREDS; i++) {
    bool live = false;
    for (int c = 0; c < WEBSOCKETS_SERVER_CLIENT_MAX; c++)
      if (_wsAuthed[c] && _wsSlot[c] == i) live = true;
    if (!live) return i;
  }
  return -1;  // full and all connected
}

void WebPortal::_saveCred(int slot) {
  Preferences p;
  p.begin("auth", false);
  char k[4];
  snprintf(k, sizeof(k), "t%d", slot);
  p.putString(k, _creds[slot].token);
  p.end();
}

void WebPortal::_clearCred(int slot) {
  _creds[slot].token[0] = 0;
  Preferences p;
  p.begin("auth", false);
  char k[4];
  snprintf(k, sizeof(k), "t%d", slot);
  p.remove(k);
  p.end();
}

void WebPortal::setMicMuted(bool muted) {
  if (muted == _micMuted) return;
  _micMuted = muted;
  if (muted && _micOn) { _micOn = false; }  // cut a live stream immediately
  _dirty = true;
  Preferences prefs;
  prefs.begin("voice", false);
  prefs.putBool("muted", muted);
  prefs.end();
}

bool WebPortal::connected() const { return WiFi.status() == WL_CONNECTED; }
String WebPortal::ip() const { return WiFi.localIP().toString(); }

// ---- WiFi setup (non-blocking captive portal) ----
void WebPortal::startConfigPortal() {
  // Hand port 80 over to WiFiManager's captive portal: flag first so the
  // core-0 http task stops polling, give it a tick to finish any in-flight
  // request, then actually close our listener.
  _configuring = true;
  if (_serverUp) {
    delay(10);
    _server.stop();
  }
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(180);
  _wm.startConfigPortal(apName());
}

void WebPortal::process() {
  if (!_configuring) return;
  _wm.process();
  if (!_wm.getConfigPortalActive()) endConfig();  // connected or timed out
}

void WebPortal::cancelConfig() {
  _wm.stopConfigPortal();
  endConfig();
}

// Leaves config mode and reclaims port 80 (re-binds our listener before the
// http task resumes polling).
void WebPortal::endConfig() {
  if (!_configuring) return;
  if (_serverUp) _server.begin();
  _configuring = false;
}

// The HTTP server serves only the static gzipped page (PROGMEM, no shared
// mutable state), so it runs on its own core-0 task. That keeps the ~250KB
// blocking page send off the render loop - loading the portal no longer
// freezes the screen. The WebSocket (commands/state) stays on the main loop.
void WebPortal::httpTask(void* arg) {
  WebPortal* self = static_cast<WebPortal*>(arg);
  for (;;) {
    // While the WiFiManager captive portal is up it owns port 80; polling our
    // server concurrently from another core would fight over the socket.
    if (!self->_configuring) self->_server.handleClient();
    vTaskDelay(1);
  }
}

// ---- HTTP + WebSocket servers ----
void WebPortal::handle() {
  if (!_serverUp && connected()) startServer();
  if (!_serverUp) return;
  _ws.loop();
  // Single broadcast point, rate-limited. Pending changes (_dirty) go out
  // quickly but coalesced. During a game the portal shows a controller (not
  // the pet mirror), so we only need score/heartbeat - NOT the ~10x/s position
  // stream, which would stall the tight game loop with TCP writes.
  const unsigned long now = millis();
  const bool inGame = !strcmp(_screenName, "doodle") || !strcmp(_screenName, "ball") ||
                      !strcmp(_screenName, "simon");
  unsigned long interval;
  if (inGame) {
    interval = 200;  // flat ~5/s: enough for the score, light on the game loop
  } else if (_dirty) {
    interval = 40;   // pending change: reflect quickly (coalesced)
  } else {
    const bool walking = _ferret && strcmp(_ferret->animName(), "walk") == 0;
    interval = walking ? 100 : 500;  // mirror the walking pet, else idle refresh
  }
  if (now - _lastBroadcast >= interval) {
    broadcastState();
    _lastBroadcast = now;
  }
  // Expire the PIN pairing window.
  if (_pairUntil && now >= _pairUntil) endPairing();
  // Push an updated connections list to every manager when it changes.
  if (_clientsDirty) {
    _clientsDirty = false;
    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
      if (!_wsAuthed[i]) continue;
      char j[512];
      clientsJson(j, sizeof(j), i);
      _ws.sendTXT(i, j);
    }
  }
  // Sweep clients that never authenticated (5s grace, checked 1x/s). Clients
  // mid-pairing ride the pairing window instead (the user is typing the PIN).
  static unsigned long lastSweep = 0;
  if (now - lastSweep > 1000) {
    lastSweep = now;
    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
      if (_wsPairing[i] && pairingActive()) continue;
      if (_wsConnAt[i] && !_wsAuthed[i] && now - _wsConnAt[i] > 5000) {
        Serial.printf("[ws] client %u dropped (no auth)\n", i);
        _ws.disconnect(i);
        _wsConnAt[i] = 0;
      }
    }
  }
  pumpMic();
}

// --- Mic PRODUCER: dedicated capture task (core 0) ---------------------------
// Continuously reads 20ms frames from the ES8311 ADC into the PSRAM ring, so
// I2S capture never waits on the network. Half-duplex: while audio plays the
// codec runs at 44.1/48k for the DAC, so we pause capture and restore the 16kHz
// mono clock (+ discard the partial-rate tail) when playback ends.
void WebPortal::micCaptureTask(void* arg) { static_cast<WebPortal*>(arg)->micCaptureLoop(); }

void WebPortal::micCaptureLoop() {
  bool wasBusy = true;  // force a 16kHz clock restore on the first real capture
  int16_t frame[320];   // 20ms @ 16kHz mono
  for (;;) {
    // Night mode is a privacy gate like mute: stop capturing at the source.
    if (!_micOn || _fullSleep) { wasBusy = true; _micRing.reset(); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    if (_audio && _audio->busy()) { wasBusy = true; vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    if (wasBusy) {
      AudioCodec::setCaptureRate();            // playback left it at 44.1/48k
      AudioCodec::readMicMono(frame, 320, 0);  // discard the partial-rate tail
      wasBusy = false;
    }
    // Block up to ~20ms for a full frame; the ring absorbs any consumer stall.
    const size_t got = AudioCodec::readMicMono(frame, 320, 25);
    if (got > 0) _micRing.write(frame, got * sizeof(int16_t));  // drops newest if full
  }
}

// --- Mic CONSUMER: drain the ring -> WS (render loop, same thread as _ws.loop
// so no locking). Sends only what's buffered; a slow/dead client can back up
// the ring (producer drops on overflow) but never blocks rendering here.
void WebPortal::pumpMic() {
  // muted OR night mode = hard gates: nothing leaves the device
  if (!_micOn || _micClient < 0 || _micMuted || _fullSleep) return;
  uint8_t buf[640];  // one 20ms frame per send (320 samples * 2 bytes)
  for (int i = 0; i < 8; i++) {   // cap the work per loop iteration
    if (_micRing.fill() < sizeof(buf)) break;
    _micRing.readAvail(buf, sizeof(buf));
    if (!_ws.sendBIN((uint8_t)_micClient, buf, sizeof(buf))) {
      // A single failed frame is usually a transient TX hiccup (HA busy for a
      // beat). Killing the mic here made always-on wake unreliable: the stream
      // flapped off, openWakeWord got silence and never fired. Just skip this
      // round and keep streaming - a truly dead client fires WStype_DISCONNECTED
      // (handled in onWsEvent), which clears _micClient/_micOn cleanly.
      break;
    }
  }
}

// --- Voice push-to-talk (called from the render loop, same thread as _ws) ---
// These only handle the mic + the ptt event; the voice UI state (listening/
// thinking/speaking) is driven by the main loop via setVoiceState().
void WebPortal::voicePttStart() {
  if (_micMuted || _fullSleep) return;  // privacy: no chime, no event, no audio
  // Ascending chime: "mic is open, go ahead". Half-duplex: capture starts
  // right after the chime finishes (the capture task waits out audio.busy()).
  if (_audio) _audio->playListen();
  _micOn = true;  // capture task streams to _micClient (HA, via voice:sub)
  if (_serverUp) sendAuthedTXT("evt:ptt:start");
  _dirty = true;
}

void WebPortal::voicePttEnd() {
  _micOn = false;
  if (_audio) _audio->playConfirm();  // descending chime: got it, processing
  if (_serverUp) sendAuthedTXT("evt:ptt:end");
  _dirty = true;
}

void WebPortal::startServer() {
  _server.on("/", [this]() { handleRoot(); });
  _server.on("/shot.bmp", [this]() { handleShot(); });  // screenshot for the portal
  _server.on("/info", [this]() { handleInfo(); });      // device info (HA discovery)
  _server.onNotFound([this]() { handleRoot(); });  // SPA fallback
  _server.begin();
  _ws.begin();
  _ws.onEvent([this](uint8_t n, WStype_t t, uint8_t* p, size_t l) { onWsEvent(n, t, p, l); });
  // Ping every 15s, expect a pong within 3s, drop after 2 misses. Without
  // this, a phone that sleeps or leaves WiFi keeps a half-open socket and
  // every broadcast BLOCKS on the dead TCP write - felt as screen freezes.
  _ws.enableHeartbeat(15000, 3000, 2);
  // mDNS: friendly access via critter.local (Bonjour/Avahi). The _critter
  // service is what the Home Assistant integration discovers via zeroconf.
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
    MDNS.addService("critter", "tcp", 81);
    MDNS.addServiceTxt("critter", "tcp", "mac", WiFi.macAddress());
  }
  _serverUp = true;
  // Serve HTTP on core 0 so a big page load can't stall the render loop.
  xTaskCreatePinnedToCore(httpTask, "http", 8192, this, 1, nullptr, 0);
  // Mic capture ring + producer task (core 0): fills the ring off the render
  // loop; handle() drains it to the WS. 48KB = ~1.5s of 16kHz mono cushion.
  if (!_micRing.capacity()) _micRing.alloc(48 * 1024);
  xTaskCreatePinnedToCore(micCaptureTask, "miccap", 4096, this, 3, nullptr, 0);
  Serial.printf("[web] portal at http://%s.local/  (ip %s, ws :81)\n", HOSTNAME, ip().c_str());
}

// GET /info -> device identity JSON (used by the HA config flow).
void WebPortal::handleInfo() {
  char name[32];
  jsonEscape(_pet ? _pet->name() : String("Furao"), name, sizeof(name));
  char b[224];
  snprintf(b, sizeof(b),
           "{\"name\":\"%s\",\"mac\":\"%s\",\"model\":\"Ball V2\",\"fw\":\"%s\","
           "\"rssi\":%d}",
           name, WiFi.macAddress().c_str(), FW_VERSION, WiFi.RSSI());
  _server.send(200, "application/json", b);
}

// Serve the React portal (single file) straight from flash, gzipped.
void WebPortal::handleRoot() {
  _server.sendHeader("Content-Encoding", "gzip");
  _server.sendHeader("Cache-Control", "no-cache");  // always fetch the current build
  _server.send_P(200, "text/html", (const char*)web_index_gz, web_index_gz_len);
}

// GET /shot.bmp -> the current screen as a 24-bit BMP. Runs on the HTTP task
// (core 0): it asks the render loop for a stable snapshot, waits briefly, then
// streams the BMP. Serving here (not the WS) keeps the ~170KB off the render.
void WebPortal::handleShot() {
  // Screenshot = privacy-sensitive, and a ?token= URL would leak the secret
  // into logs/history. Challenge-response instead: a bare GET answers 401
  // with a one-shot nonce; retry with ?sig=HMAC-SHA256(token, "shot:<nonce>").
  if (!_server.hasArg("sig")) {
    randHex(_shotNonce, 32);
    _shotNonceAt = millis();
    char body[48];
    snprintf(body, sizeof(body), "nonce:%s", _shotNonce);
    _server.send(401, "text/plain", body);
    return;
  }
  char m[48];
  snprintf(m, sizeof(m), "shot:%s", _shotNonce);
  const bool fresh = _shotNonce[0] && millis() - _shotNonceAt < 60000;
  const String sig = _server.arg("sig");
  bool ok = false;
  if (fresh) {  // any credential may sign a screenshot request
    for (int i = 0; i < MAX_CREDS; i++) {
      if (!_creds[i].token[0]) continue;
      char expect[65];
      hmacHex(_creds[i].token, m, expect);
      if (sig.equalsIgnoreCase(expect)) { ok = true; break; }
    }
  }
  if (!ok) { _server.send(401, "text/plain", "unauthorized"); return; }
  _shotNonce[0] = 0;  // single-use
  if (!_renderer) { _server.send(503, "text/plain", "no renderer"); return; }
  _renderer->requestWebSnapshot();
  const unsigned long t0 = millis();
  while (!_renderer->webSnapshotReady() && millis() - t0 < 1500) delay(3);
  if (!_renderer->webSnapshotReady()) { _server.send(504, "text/plain", "timeout"); return; }

  const uint8_t* px = (const uint8_t*)_renderer->webSnapshot();
  const int W = 240, H = 240;
  const uint32_t dataSize = (uint32_t)W * H * 3;
  const uint32_t fileSize = 54 + dataSize;
  if (!_bmp) {
    _bmp = (uint8_t*)ps_malloc(fileSize);
    if (!_bmp) _bmp = (uint8_t*)malloc(fileSize);
  }
  if (!px || !_bmp) { _server.send(500, "text/plain", "no buffer"); return; }

  // Assemble the whole BMP once, then send it in a single blocking write - many
  // small chunked writes were truncating the response.
  memset(_bmp, 0, 54);
  _bmp[0] = 'B'; _bmp[1] = 'M';
  _bmp[2] = fileSize; _bmp[3] = fileSize >> 8; _bmp[4] = fileSize >> 16; _bmp[5] = fileSize >> 24;
  _bmp[10] = 54;                          // pixel data offset
  _bmp[14] = 40;                          // DIB header size
  _bmp[18] = W; _bmp[19] = W >> 8;
  _bmp[22] = H; _bmp[23] = H >> 8;        // positive height -> bottom-up rows
  _bmp[26] = 1;                           // planes
  _bmp[28] = 24;                          // bits per pixel
  _bmp[34] = dataSize; _bmp[35] = dataSize >> 8; _bmp[36] = dataSize >> 16; _bmp[37] = dataSize >> 24;

  uint8_t* out = _bmp + 54;
  for (int y = H - 1; y >= 0; y--) {  // BMP stores the bottom row first
    const uint8_t* p = px + (uint32_t)y * W * 2;
    for (int x = 0; x < W; x++) {
      const uint16_t v = (p[2 * x] << 8) | p[2 * x + 1];  // canvas stores big-endian
      *out++ = (v & 0x1F) << 3;          // B
      *out++ = ((v >> 5) & 0x3F) << 2;   // G
      *out++ = ((v >> 11) & 0x1F) << 3;  // R
    }
  }
  _renderer->clearWebSnapshot();

  _server.setContentLength(fileSize);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "image/bmp", "");
  _server.client().write(_bmp, fileSize);  // WiFiClient::write blocks until sent
}

void WebPortal::onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[ws] client %u connected (%s) - challenging\n", num,
                  _ws.remoteIP(num).toString().c_str());
    _wsAuthed[num] = false;
    _wsConnAt[num] = millis();  // unauth grace timer (swept in handle())
    // Challenge-response: fresh nonce per socket; the client must answer
    // with HMAC-SHA256(token, nonce) - the token itself never on the wire.
    randHex(_wsNonce[num], 32);
    char ch[48];
    snprintf(ch, sizeof(ch), "challenge:%s", _wsNonce[num]);
    _ws.sendTXT(num, ch);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[ws] client %u disconnected\n", num);
    const bool was = _wsAuthed[num];
    _wsAuthed[num] = false;
    _wsSlot[num] = -1;
    _wsLabel[num][0] = 0;
    _wsPairing[num] = false;
    _wsConnAt[num] = 0;
    if ((int)num == _micClient) { _micOn = false; _micClient = -1; }
    if ((int)num == _assistClient) { _assistOn = false; _assistClient = -1; }
    if (was) _clientsDirty = true;  // refresh the connections list
  } else if (type == WStype_TEXT) {
    String msg;
    msg.reserve(len);
    for (size_t i = 0; i < len; i++) msg += (char)payload[i];
    // Auth gate: the FIRST message must be "auth:<token>" (paired clients)
    // or the PIN pairing dance (new clients). Until then the client gets
    // nothing and can do nothing. Protects commands, state, shots and the MIC.
    if (!_wsAuthed[num]) {
      if (msg.startsWith("auth:")) {
        // "auth:<hmac>[:<client_nonce>]" - hmac = HMAC(token, our nonce). We
        // don't know WHICH credential, so try every stored token; the match
        // identifies the client's slot. Optional client nonce -> mutual auth
        // (proof:<HMAC(that token, client_nonce)>), the mDNS-spoof defense.
        const String body = msg.substring(5);
        const int sep = body.indexOf(':');
        const String mac = sep >= 0 ? body.substring(0, sep) : body;
        int slot = -1;
        if (_wsNonce[num][0]) {
          for (int i = 0; i < MAX_CREDS; i++) {
            if (!_creds[i].token[0]) continue;
            char expect[65];
            hmacHex(_creds[i].token, _wsNonce[num], expect);
            if (mac.equalsIgnoreCase(expect)) { slot = i; break; }
          }
        }
        if (slot >= 0) {
          _wsAuthed[num] = true;
          _wsSlot[num] = slot;
          _wsNonce[num][0] = 0;  // single-use
          _clientsDirty = true;
          Serial.printf("[ws] client %u authenticated (slot %d)\n", num, slot);
          if (sep >= 0) {
            char proofMac[65], proof[80];
            hmacHex(_creds[slot].token, body.substring(sep + 1).c_str(), proofMac);
            snprintf(proof, sizeof(proof), "proof:%s", proofMac);
            _ws.sendTXT(num, proof);
          }
          char buf[768];
          stateJson(buf, sizeof(buf));
          _ws.sendTXT(num, buf);  // now it may see the state
        } else {
          Serial.printf("[ws] client %u rejected (bad hmac)\n", num);
          _ws.disconnect(num);
        }
      } else if (msg == "pair:start") {
        // New client asks to pair: pop the PIN on the screen and keep this
        // socket alive (exempt from the no-auth sweep) while the window is
        // open, so it can submit the PIN the user reads.
        startPairing();
        _wsPairing[num] = true;
        Serial.printf("[ws] client %u pairing (pin on screen)\n", num);
      } else if (msg.startsWith("pair:") && pairingActive() &&
                 msg.substring(5) == _pairPin) {
        // Correct PIN: mint a NEW per-client token in a free slot and hand it
        // over (the one moment a token is on the wire - supervised handover).
        int slot = _freeCredSlot();
        if (slot < 0) {  // table full and everyone's connected
          _ws.sendTXT(num, "err:full");
          _ws.disconnect(num);
          endPairing();
          return;
        }
        randHex(_creds[slot].token, 16);
        _saveCred(slot);
        _wsAuthed[num] = true;
        _wsSlot[num] = slot;
        _wsPairing[num] = false;
        _clientsDirty = true;
        endPairing();
        char rep[32];
        snprintf(rep, sizeof(rep), "token:%s", _creds[slot].token);
        _ws.sendTXT(num, rep);
        char buf[768];
        stateJson(buf, sizeof(buf));
        _ws.sendTXT(num, buf);
        Serial.printf("[ws] client %u paired -> slot %d\n", num, slot);
      } else if (msg.startsWith("pair:")) {
        if (pairingActive() && ++_pairAttempts >= 3) endPairing();  // brute guard
        Serial.printf("[ws] client %u wrong PIN\n", num);
        _ws.disconnect(num);
      } else {
        Serial.printf("[ws] client %u rejected (bad auth)\n", num);
        _ws.disconnect(num);
      }
      return;
    }
    // Client names itself for the connections manager (portal/HA send this
    // right after auth). Per-connection, so sockets sharing a credential still
    // show distinct names; re-sent on every connect (no need to persist).
    if (msg.startsWith("label:")) {
      strlcpy(_wsLabel[num], msg.substring(6).c_str(), sizeof(_wsLabel[num]));
      _clientsDirty = true;
      return;
    }
    // HA control panel: the plugin pushes the exposed entities here.
    if (msg.startsWith("ha:list:")) { if (_ha) _ha->setList(msg.c_str() + 8); return; }
    if (msg.startsWith("ha:upd:"))  { if (_ha) _ha->applyUpdate(msg.c_str() + 7); return; }
    // Connections manager: request the list, or revoke a slot / everyone.
    if (msg == "clients?") { char j[512]; clientsJson(j, sizeof(j), num);
                             _ws.sendTXT(num, j); return; }
    if (msg.startsWith("revoke:")) {
      const String w = msg.substring(7);
      if (w == "all") revokeAll();
      else revokeSlot(w.toInt());
      return;
    }
    // Mic control needs the client num (audio is streamed back to it).
    // Muted = ignore any request to open the mic (privacy wins over HA).
    if (msg == "mic:on")  { if (_micMuted || _fullSleep) return; _micClient = num; _micOn = true; _dirty = true; return; }
    if (msg == "mic:off") { _micOn = false; _micClient = -1;  _dirty = true; return; }
    // Privacy mute toggle (portal switch / HA switch / BOOT tap via main).
    if (msg == "mute:on")  { setMicMuted(true);  return; }
    if (msg == "mute:off") { setMicMuted(false); return; }
    // HA registers as the voice audio sink WITHOUT starting the stream; the
    // device gates streaming by the BOOT button (push-to-talk, Phase 2).
    if (msg == "voice:sub") { _micClient = num; return; }
    // Software push-to-talk (portal/phone button, and test harness): same path
    // as holding BOOT. If nobody subscribed, target the sender so it can also
    // consume the audio directly.
    if (msg == "ptt:start") { if (_micClient < 0) _micClient = num; voicePttStart(); return; }
    if (msg == "ptt:end")   { voicePttEnd(); return; }
    // Weather location from the portal (browser did the geocoding), the HA
    // plugin (home coords, once) or serial: "wxloc:<lat>,<lon>,<city>".
    if (msg.startsWith("wxloc:")) {
      const String rest = msg.substring(6);
      const int c1 = rest.indexOf(','), c2 = rest.indexOf(',', c1 + 1);
      if (c1 > 0 && _weather) {
        const float lat = rest.substring(0, c1).toFloat();
        const float lon = (c2 > 0 ? rest.substring(c1 + 1, c2)
                                  : rest.substring(c1 + 1)).toFloat();
        const String city = c2 > 0 ? rest.substring(c2 + 1) : String("Local");
        _weather->setLocation(lat, lon, city.c_str());
        _dirty = true;
      }
      return;
    }
    // Dev panel (portal): force a scene weather / fire a lightning strike.
    if (msg.startsWith("wxset:")) {
      _wxSetReq = Weather::codeFromName(msg.substring(6).c_str());
      return;
    }
    if (msg == "wxbolt") {
      if (_renderer) _renderer->triggerBolt();
      return;
    }
    // Pipeline liveness from the satellite: assist:on when a wake/STT stage
    // actually starts consuming our audio, assist:off when the run dies.
    // Gates the header's "live mic" icon so it reflects END-TO-END health.
    if (msg == "assist:on")  { _assistOn = true;  _assistClient = num; _dirty = true; return; }
    if (msg == "assist:off") { _assistOn = false; _assistClient = -1;  _dirty = true; return; }
    // Voice pipeline state pushed by HA (wake word runs): drives the ring/LED.
    // (after "voice:sub" above, which must match first)
    if (msg.startsWith("voice:")) {
      const String s = msg.substring(6);
      _voiceCmd = s == "listening" ? 1 : s == "thinking" ? 2 : s == "speaking" ? 3 : 0;
      return;
    }
    // Full sleep (night mode): screen + LED off, pet asleep. Main applies it.
    if (msg == "fullsleep:on")  { _fullSleepReq = 1; return; }
    if (msg == "fullsleep:off") { _fullSleepReq = 0; return; }
    // Night-mode sound settings (full-sleep transitions only).
    if (msg == "sleepsnd:on")  { _sleepSndReq = 1; return; }
    if (msg == "sleepsnd:off") { _sleepSndReq = 0; return; }
    if (msg == "wakesnd:on")   { _wakeSndReq = 1; return; }
    if (msg == "wakesnd:off")  { _wakeSndReq = 0; return; }
    if (msg.startsWith("micgain:")) {  // bring-up: tune ADC gain live
      if (_audio) _audio->setMicGain(msg.substring(8).toInt());
      return;
    }
    applyCommand(msg);
    // Don't broadcast from here: just mark dirty and let handle() send one
    // coalesced frame. Joystick input ("game:x:...") never marks dirty.
    if (!msg.startsWith("game:x:")) _dirty = true;
  }
}

// Commands from React: "feed"/"pat"/"clean"/"sleep", "name:NewName", "vol:N",
// "led:N", "clock:on|off", "fmt:12|24", "tz:<posix>", "idle:<sec>", "menu:<sec>",
// plus game control: "game:start" (Jump!), "game:ball" (Bolinha), "game:simon"
// (Genius), "game:back", "game:x:<0..1>" (Jump! steering), "ball:t:<nx>:<ny>"
// (Bolinha throw), "simon:<0..3>" (Genius pad press).
void WebPortal::applyCommand(const String& msg) {
  // Game controller (Doodle Jump from the phone).
  if (msg.startsWith("game:x:")) {
    _gameTx = msg.substring(7).toFloat();  // normalized [0..1]
    _gameTxMs = millis();
    return;
  }
  if (msg == "game:start") { _navReq = NAV_START; return; }
  if (msg == "game:ball")  { _navReq = NAV_BALL;  return; }
  if (msg == "game:simon") { _navReq = NAV_SIMON; return; }
  if (msg == "game:back")  { _navReq = NAV_BACK;  return; }
  if (msg.startsWith("simon:")) {  // "simon:<0..3>" color pad press
    _simonPress = constrain(msg.substring(6).toInt(), 0, 3);
    return;
  }
  if (msg.startsWith("ball:t:")) {  // "ball:t:<nx>:<ny>" normalized swipe
    const int sep = msg.indexOf(':', 7);
    if (sep > 0) {
      _throwNx = msg.substring(7, sep).toFloat();
      _throwNy = msg.substring(sep + 1).toFloat();
      _throwReq = true;
    }
    return;
  }
  if (msg.startsWith("name:")) {
    if (_pet) _pet->setName(msg.substring(5));
    return;
  }
  if (msg.startsWith("vol:")) {
    if (_audio) _audio->setVolume(msg.substring(4).toInt());
    return;
  }
  if (msg.startsWith("led:")) {
    if (_led) _led->setBrightness(msg.substring(4).toInt());
    return;
  }
  if (msg.startsWith("scr:")) {
    if (_renderer) _renderer->setScreenBrightness(msg.substring(4).toInt());
    return;
  }
  // Media player (HA integration): "media:play:<url>" / "media:stop".
  if (msg.startsWith("media:play:")) {
    Serial.printf("[media] request: %s\n", msg.c_str() + 11);
    if (_audio) _audio->playStream(msg.c_str() + 11);
    return;
  }
  if (msg == "media:stop") {
    if (_audio) _audio->stopStream();
    return;
  }
  if (_clock) {
    if (msg == "clock:on")  { _clock->setEnabled(true);  return; }
    if (msg == "clock:off") { _clock->setEnabled(false); return; }
    if (msg == "fmt:24")    { _clock->setH24(true);  return; }
    if (msg == "fmt:12")    { _clock->setH24(false); return; }
    if (msg == "date:dmy")  { _clock->setDateDmy(true);  return; }
    if (msg == "date:mdy")  { _clock->setDateDmy(false); return; }
    if (msg.startsWith("tz:"))   { _clock->setTz(msg.substring(3)); return; }
    if (msg.startsWith("idle:")) { _clock->setIdleSec(msg.substring(5).toInt()); return; }
    if (msg.startsWith("menu:")) { _clock->setMenuTimeoutSec(msg.substring(5).toInt()); return; }
  }
  if (!_onAction) return;
  if (msg == "feed")       _onAction(ACTION_FEED);
  else if (msg == "pat")   _onAction(ACTION_PAT);
  else if (msg == "clean") _onAction(ACTION_CLEAN);
  else if (msg == "sleep") _onAction(ACTION_TOGGLE_SLEEP);
}

WebPortal::GameNav WebPortal::consumeGameNav() {
  GameNav n = _navReq;
  _navReq = NAV_NONE;
  return n;
}

bool WebPortal::consumeBallThrow(float& nx, float& ny) {
  if (!_throwReq) return false;
  _throwReq = false;
  nx = _throwNx;
  ny = _throwNy;
  return true;
}

bool WebPortal::consumeSimonPress(int& color) {
  if (_simonPress < 0) return false;
  color = _simonPress;
  _simonPress = -1;
  return true;
}

// Latest horizontal target from the phone, or -1 if none/stale (finger lifted
// or connection dropped mid-drag -> stop steering rather than drift).
float WebPortal::gameTargetXNorm() {
  if (_gameTx < 0) return -1.0f;
  if (millis() - _gameTxMs > 400) return -1.0f;
  return _gameTx;
}

void WebPortal::broadcastState() {
  if (!_serverUp) return;
  _dirty = false;
  if (_ws.connectedClients() == 0) return;  // nobody listening: skip the work
  char buf[768];
  stateJson(buf, sizeof(buf));
  sendAuthedTXT(buf);
}

// HA panel control: broadcast the command to authed clients (the plugin acts,
// the portal ignores it - same pattern as evt:).
void WebPortal::sendHaCmd(const char* entityId, const char* action) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "ha:cmd:%s:%s", entityId, action);
  sendAuthedTXT(cmd);
}

void WebPortal::haSubscribe() { sendAuthedTXT("ha:sub"); }

// Broadcast to AUTHENTICATED clients only (broadcastTXT would leak state to
// sockets that never presented the pairing token).
void WebPortal::sendAuthedTXT(const char* msg) {
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (_wsAuthed[i]) _ws.sendTXT(i, (uint8_t*)msg, strlen(msg));
  }
}

// Fill out with hexChars random hex digits (+ NUL).
void WebPortal::randHex(char* out, size_t hexChars) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < hexChars; i++) out[i] = H[esp_random() & 0xF];
  out[hexChars] = 0;
}

// HMAC-SHA256(key, msg) -> lowercase hex (65 bytes incl. NUL).
void WebPortal::hmacHex(const char* key, const char* msg, char out65[65]) {
  uint8_t mac[32] = {0};
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, (const uint8_t*)key, strlen(key),
                  (const uint8_t*)msg, strlen(msg), mac);
  static const char* H = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out65[i * 2] = H[mac[i] >> 4];
    out65[i * 2 + 1] = H[mac[i] & 0xF];
  }
  out65[64] = 0;
}

// Open (or refresh) the PIN pairing window: a fresh random 6-digit code,
// 90s deadline, 3 attempts. main() mirrors it on screen automatically.
void WebPortal::startPairing() {
  if (!pairingActive()) {
    snprintf(_pairPin, sizeof(_pairPin), "%06u",
             (unsigned)(esp_random() % 900000u + 100000u));
    _pairAttempts = 0;
  }
  _pairUntil = millis() + 90000;  // starting again just extends the window
}

void WebPortal::endPairing() {
  _pairUntil = 0;
  _pairPin[0] = 0;
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) _wsPairing[i] = false;
}

void WebPortal::cancelPairing() { endPairing(); }

// Wipe EVERY credential and kick everyone: all clients must PIN-pair again.
// The hardware "Revogar acesso" button + "revoke:all".
void WebPortal::revokeAll() {
  endPairing();
  for (int i = 0; i < MAX_CREDS; i++) if (_creds[i].token[0]) _clearCred(i);
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (_wsAuthed[i]) _ws.disconnect(i);
    _wsAuthed[i] = false;
    _wsSlot[i] = -1;
  }
  _micOn = false;
  _micClient = -1;
  Serial.println("[auth] ALL credentials revoked");
}

// Revoke ONE credential: wipe the slot and kick sockets using it; the other
// clients keep their own tokens. The portal/HA "encerrar conexao" action.
void WebPortal::revokeSlot(int slot) {
  if (slot < 0 || slot >= MAX_CREDS || !_creds[slot].token[0]) return;
  _clearCred(slot);
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (_wsAuthed[i] && _wsSlot[i] == slot) {
      if ((int)i == _micClient) { _micOn = false; _micClient = -1; }
      _ws.disconnect(i);
      _wsAuthed[i] = false;
      _wsSlot[i] = -1;
    }
  }
  _clientsDirty = true;
  Serial.printf("[auth] revoked slot %d\n", slot);
}

// JSON array of connected clients for the manager: [{"slot":N,"ip":"..",
// "label":"..","me":bool}]. `selfNum` = the recipient's own socket number.
void WebPortal::clientsJson(char* out, size_t n, int selfNum) {
  size_t o = 0;
  o += snprintf(out + o, n - o, "clients:[");
  bool first = true;
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (!_wsAuthed[i]) continue;
    const char* lbl = _wsLabel[i][0] ? _wsLabel[i] : "Aparelho";
    o += snprintf(out + o, n > o ? n - o : 0,
                  "%s{\"slot\":%d,\"ip\":\"%s\",\"label\":\"%s\",\"me\":%s}",
                  first ? "" : ",", _wsSlot[i],
                  _ws.remoteIP(i).toString().c_str(), lbl,
                  (int)i == selfNum ? "true" : "false");
    first = false;
  }
  snprintf(out + o, n > o ? n - o : 0, "]");
}

// "label - ip", one per connected client (hardware Conexoes page, view-only).
void WebPortal::clientsInfo(char* out, size_t n) {
  size_t o = 0;
  int cnt = 0;
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (!_wsAuthed[i]) continue;
    cnt++;
    const char* lbl = _wsLabel[i][0] ? _wsLabel[i] : "Aparelho";
    const String ip = _ws.remoteIP(i).toString();
    o += snprintf(out + o, n > o ? n - o : 0, "%s%s", o ? "\n" : "", lbl);
    o += snprintf(out + o, n > o ? n - o : 0, "\n %s", ip.c_str());
  }
  if (!cnt) strlcpy(out, "Nenhum cliente conectado", n);
}

static const char* moodName(Mood m) {
  switch (m) {
    case MOOD_HAPPY:   return "happy";
    case MOOD_NEUTRAL: return "neutral";
    case MOOD_SAD:     return "sad";
    case MOOD_HUNGRY:  return "hungry";
    case MOOD_SLEEPY:  return "sleepy";
    case MOOD_DIRTY:   return "dirty";
  }
  return "neutral";
}

// Escapes '"' and '\' and drops control chars - the pet name is the only
// free-text field and a quote in it would corrupt the whole state JSON.
static void jsonEscape(const String& in, char* out, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < in.length() && o + 2 < n; i++) {
    const char ch = in[i];
    if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = ch; }
    else if ((uint8_t)ch >= 0x20) out[o++] = ch;
  }
  out[o] = '\0';
}

// One snprintf into a stack buffer: no String concatenation churn (the old
// version did ~40 heap allocations per frame, up to 10x/s -> fragmentation
// and avoidable latency on the render loop).
void WebPortal::stateJson(char* out, size_t n) const {
  const Pet& p = *_pet;
  char name[32];
  jsonEscape(p.name(), name, sizeof(name));
  snprintf(out, n,
           "{\"screen\":\"%s\",\"score\":%d,\"battery\":%d,\"name\":\"%s\",\"sleeping\":%s,"
           "\"fullSleep\":%s,\"sleepSnd\":%s,\"wakeSnd\":%s,\"micMuted\":%s,"
           "\"micOn\":%s,\"micClient\":%d,\"assistOn\":%s,\"micLive\":%s,"
           "\"wxCity\":\"%s\","
           "\"mood\":\"%s\",\"media\":\"%s\",\"voice\":\"%s\","
           "\"volume\":%d,\"ledBright\":%d,\"scrBright\":%d,\"clockOn\":%s,\"tz\":\"%s\","
           "\"idleSec\":%d,\"menuSec\":%d,\"h24\":%s,\"dmy\":%s,"
           "\"anim\":\"%s\",\"seq\":%u,\"flip\":%s,\"x\":%.3f,"
           "\"hunger\":%.1f,\"energy\":%.1f,\"joy\":%.1f,\"hygiene\":%.1f}",
           _screenName, _gameScore, _battery, name,
           p.sleeping() ? "true" : "false",
           _fullSleep ? "true" : "false",
           _sleepSnd ? "true" : "false",
           _wakeSnd ? "true" : "false",
           _micMuted ? "true" : "false",
           _micOn ? "true" : "false", _micClient, _assistOn ? "true" : "false",
           micLive() ? "true" : "false",
           _weather ? _weather->city() : "",
           moodName(p.mood()),
           _audio && _audio->streaming() ? "play" : "idle",
           _voiceState,
           _audio ? _audio->volume() : 0,
           _led ? _led->brightness() : 50,
           _renderer ? _renderer->screenBrightness() : 70,
           _clock && _clock->enabled() ? "true" : "false",
           _clock ? _clock->tz().c_str() : "",
           _clock ? _clock->idleSec() : 30,
           _clock ? _clock->menuTimeoutSec() : 15,
           _clock && _clock->h24() ? "true" : "false",
           (!_clock || _clock->dateDmy()) ? "true" : "false",
           _ferret ? _ferret->animName() : "idle",
           _ferret ? (unsigned)_ferret->animSeq() : 0u,
           _ferret && _ferret->faceLeft() ? "true" : "false",
           _ferret ? _ferret->xNorm() : 0.5f,
           p.hunger(), p.energy(), p.joy(), p.hygiene());
}
