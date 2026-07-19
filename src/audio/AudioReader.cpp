#include "AudioReader.h"
#include <esp_http_client.h>
#include <esp_task_wdt.h>
#include "../TaskRegistry.h"

// Client config mirrors Voice PE's reader (validated by netbench nb2:
// ~280 KB/s sustained). timeout_ms is deliberately short: esp_http_client_read
// blocks up to this long when a paced source (Music Assistant) has no data
// yet, and stop() responsiveness is bounded by it.
static constexpr int HTTP_TIMEOUT_MS = 1500;
static constexpr int HTTP_HEADER_BUF = 2048;
static constexpr uint32_t READ_CHUNK_MAX = 16 * 1024;
// Give up if the server sends nothing at all for this long (the consumer has
// its own 15s grace; this keeps the two roughly in step).
static constexpr uint32_t STALL_MS = 15000;

void AudioReader::begin(StreamRing* ring) {
  _ring = ring;
  // Core 0 (PRO, with WiFi/lwIP), prio 3: above the WebPortal http task (1),
  // below nothing latency-critical. 6KB stack fits esp_http_client (no TLS).
  xTaskCreatePinnedToCore(taskTrampoline, "audioread", 6144, this, 3, &_task, 0);
  taskreg::add("audioread", _task, 0, 3);
}

bool AudioReader::start(const char* url) {
  if (_state != State::Idle) return false;
  if (strncmp(url, "http://", 7) != 0) return false;  // https: no TLS budget yet
  if (strlen(url) >= sizeof(_url)) return false;
  strcpy(_url, url);
  _abort = false;
  _contentLength = -1;
  _state = State::Running;      // set BEFORE the wake so idle() is immediately false
  xTaskNotifyGive(_task);
  return true;
}

void AudioReader::taskTrampoline(void* arg) {
  static_cast<AudioReader*>(arg)->taskLoop();
}

void AudioReader::taskLoop() {
  esp_task_wdt_add(nullptr);  // watched: a wedged network read reboots the device
  for (;;) {
    // Parked until start(); the timed wait exists only to feed the watchdog.
    esp_task_wdt_reset();
    if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000))) continue;
    fetch();
    _ring->setEof();  // success and failure both just end the stream
    _state = State::Idle;
  }
}

void AudioReader::fetch() {
  esp_http_client_config_t cfg = {};
  cfg.url = _url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.buffer_size = HTTP_HEADER_BUF;
  cfg.keep_alive_enable = true;
  cfg.max_redirection_count = 5;
  cfg.user_agent = "HomeCritters";

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    Serial.println("[reader] init failed");
    return;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    Serial.printf("[reader] open failed: %s\n", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return;
  }
  _contentLength = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200 && status != 206) {
    Serial.printf("[reader] http %d\n", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  uint32_t lastData = millis();
  while (!_abort) {
    esp_task_wdt_reset();  // every wait in here is bounded (timeout_ms / ticks)
    uint32_t contig;
    uint8_t* dst = _ring->writeRegion(contig);
    if (!dst) {  // ring full: decoder is behind, give it room
      vTaskDelay(2);
      continue;
    }
    // Zero-copy: the SDK writes straight into the ring (de-chunked bytes).
    const int n = esp_http_client_read(client, (char*)dst,
                                       min(contig, READ_CHUNK_MAX));
    if (n > 0) {
      _ring->commit((uint32_t)n);
      lastData = millis();
      continue;
    }
    if (n == 0) {
      if (esp_http_client_is_complete_data_received(client)) break;  // clean EOF
      if (millis() - lastData > STALL_MS) {
        Serial.println("[reader] stalled, giving up");
        break;
      }
      continue;  // paced source: read timed out with nothing new, retry
    }
    Serial.println("[reader] read error");
    break;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}
