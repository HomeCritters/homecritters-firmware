#include "NetBench.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

namespace netbench {

// How long each bench runs at most (or until the body ends).
static constexpr uint32_t BENCH_MS = 15000;
static constexpr uint32_t READ_CHUNK = 16 * 1024;

static volatile bool s_busy = false;
static char s_url[224];
static bool s_useIdf = false;

// Shared per-second reporter. Feed it byte counts; it prints one line per
// second and a summary at the end.
struct Meter {
  uint32_t t0 = 0, lastTick = 0, secBytes = 0, total = 0;
  uint32_t minKps = 0xFFFFFFFF, maxKps = 0, secs = 0;
  void begin() { t0 = lastTick = millis(); }
  void add(uint32_t n) {
    secBytes += n;
    total += n;
    const uint32_t now = millis();
    if (now - lastTick >= 1000) {
      const uint32_t kps = secBytes / 1024;
      Serial.printf("[nb] %2us  %4u KB/s\n", (unsigned)(++secs), (unsigned)kps);
      if (kps < minKps) minKps = kps;
      if (kps > maxKps) maxKps = kps;
      secBytes = 0;
      lastTick = now;
    }
  }
  void finish(const char* how) {
    const uint32_t elapsed = millis() - t0;
    Serial.printf("[nb] done (%s): %u KB in %.1fs -> avg %u KB/s (min %u, max %u), rssi %d\n",
                  how, (unsigned)(total / 1024), elapsed / 1000.0f,
                  (unsigned)(elapsed ? (total / 1024) * 1000 / elapsed : 0),
                  (unsigned)(minKps == 0xFFFFFFFF ? 0 : minKps), (unsigned)maxKps,
                  (int)WiFi.RSSI());
  }
};

// ---- raw WiFiClient variant: measures the pure TCP+lwIP ceiling ----
static void benchRaw(uint8_t* buf) {
  // Parse http://host[:port]/path
  const char* p = s_url + 7;  // past "http://"
  const char* slash = strchr(p, '/');
  const char* path = slash ? slash : "/";
  char host[128];
  size_t hostLen = slash ? (size_t)(slash - p) : strlen(p);
  if (hostLen >= sizeof(host)) { Serial.println("[nb] host too long"); return; }
  memcpy(host, p, hostLen);
  host[hostLen] = 0;
  uint16_t port = 80;
  if (char* colon = strchr(host, ':')) {
    *colon = 0;
    port = atoi(colon + 1);
  }

  WiFiClient client;
  client.setNoDelay(true);
  if (!client.connect(host, port, 5000)) {
    Serial.printf("[nb] connect failed: %s:%u\n", host, port);
    return;
  }
  client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: HomeCritters\r\nConnection: close\r\n\r\n",
                path, host);

  // Skip headers (read until CRLFCRLF).
  uint32_t match = 0, hdrT0 = millis();
  while (match < 4 && millis() - hdrT0 < 5000) {
    int c = client.read();
    if (c < 0) { delay(1); continue; }
    match = (c == ((match & 1) ? '\n' : '\r')) ? match + 1 : (c == '\r' ? 1 : 0);
  }
  if (match < 4) { Serial.println("[nb] no header end"); client.stop(); return; }

  Meter m;
  m.begin();
  while (millis() - m.t0 < BENCH_MS) {
    int avail = client.available();
    if (avail > 0) {
      int n = client.read(buf, min((uint32_t)avail, READ_CHUNK));
      if (n > 0) { m.add(n); continue; }
    }
    if (!client.connected() && client.available() == 0) { m.finish("eof"); client.stop(); return; }
    vTaskDelay(1);
  }
  m.finish("15s");
  client.stop();
}

// ---- esp_http_client variant: mirrors the future AudioReader config ----
static void benchIdf(uint8_t* buf) {
  esp_http_client_config_t cfg = {};
  cfg.url = s_url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 5000;              // same as Voice PE's reader
  cfg.buffer_size = 2048;             // header buffer, Voice PE value
  cfg.keep_alive_enable = true;
  cfg.max_redirection_count = 5;
  cfg.user_agent = "HomeCritters";

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) { Serial.println("[nb2] init failed"); return; }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    Serial.printf("[nb2] open failed: %s\n", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return;
  }
  int64_t len = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  Serial.printf("[nb2] http %d, content-length %lld\n", status, (long long)len);

  Meter m;
  m.begin();
  while (millis() - m.t0 < BENCH_MS) {
    int n = esp_http_client_read(client, (char*)buf, READ_CHUNK);
    if (n > 0) { m.add(n); continue; }
    if (n == 0 && esp_http_client_is_complete_data_received(client)) {
      m.finish("eof");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return;
    }
    if (n < 0) { Serial.println("[nb2] read error"); break; }
    vTaskDelay(1);
  }
  m.finish("15s");
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

static void benchTask(void*) {
  // Read buffer in PSRAM: same memory the streaming ring will use.
  uint8_t* buf = (uint8_t*)ps_malloc(READ_CHUNK);
  if (!buf) buf = (uint8_t*)malloc(READ_CHUNK);
  if (buf) {
    Serial.printf("[nb] %s %s\n", s_useIdf ? "esp_http_client" : "raw WiFiClient", s_url);
    s_useIdf ? benchIdf(buf) : benchRaw(buf);
    free(buf);
  } else {
    Serial.println("[nb] no memory");
  }
  s_busy = false;
  vTaskDelete(nullptr);
}

static void start(const char* url, bool useIdf) {
  if (s_busy) { Serial.println("[nb] busy - wait for the current run"); return; }
  if (strncmp(url, "http://", 7) != 0) { Serial.println("[nb] http:// only"); return; }
  if (strlen(url) >= sizeof(s_url)) { Serial.println("[nb] url too long"); return; }
  strcpy(s_url, url);
  s_useIdf = useIdf;
  s_busy = true;
  // Core 0 prio 3: exactly where/how the future AudioReader task will run.
  xTaskCreatePinnedToCore(benchTask, "netbench", 8192, nullptr, 3, nullptr, 0);
}

void runRaw(const char* url) { start(url, false); }
void runIdf(const char* url) { start(url, true); }

// Poor-man's CPU probe: prio-0 counter tasks on each core for 3s. The counts
// are proportional to idle CPU on that core - compare idle vs during playback
// to find who is stealing time.
static volatile uint32_t s_cnt0 = 0, s_cnt1 = 0;
static void counterTask(void* arg) {
  volatile uint32_t* c = (volatile uint32_t*)arg;
  const uint32_t t0 = millis();
  while (millis() - t0 < 3000) (*c)++;
  vTaskDelete(nullptr);
}

void cpuReport() {
  s_cnt0 = s_cnt1 = 0;
  xTaskCreatePinnedToCore(counterTask, "cnt0", 2048, (void*)&s_cnt0, 0, nullptr, 0);
  xTaskCreatePinnedToCore(counterTask, "cnt1", 2048, (void*)&s_cnt1, 0, nullptr, 1);
  vTaskDelay(pdMS_TO_TICKS(3200));
  Serial.printf("[cpu] free-ish core0=%u core1=%u (counts/3s, higher = more idle)\n",
                (unsigned)s_cnt0, (unsigned)s_cnt1);
}

// Print the CURRENT WiFi power-save mode and force it to NONE. If playback
// somehow re-enables modem sleep, this shows it and un-does it live.
void wifiPsReport() {
  wifi_ps_type_t ps;
  esp_wifi_get_ps(&ps);
  Serial.printf("[wifi] ps was %d (0=NONE 1=MIN 2=MAX) -> forcing NONE\n", (int)ps);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void memReport() {
  Serial.printf("[mem] heap free %u KB (largest %u KB) | psram free %u KB (largest %u KB)\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
}

}  // namespace netbench
