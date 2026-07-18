#include "Weather.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_http_client.h>
// The IDF cert-bundle header isn't on the Arduino variant include path, but
// the symbol lives in the prebuilt libs (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

// Fetch cadence (see plan): 30 min normal, 5 min retry after a failure.
static constexpr unsigned long FETCH_OK_MS   = 30UL * 60 * 1000;
static constexpr unsigned long FETCH_FAIL_MS = 5UL * 60 * 1000;
static constexpr int HTTP_TIMEOUT_MS = 8000;
static constexpr size_t BODY_MAX = 4096;  // Open-Meteo reply is ~1KB

// ---------- WMO code mapping ----------
WxKind Weather::kindFromCode(uint8_t c) {
  if (c <= 1) return WX_CLEAR;                     // 0-1 clear / mainly clear
  if (c <= 3) return WX_CLOUDY;                    // 2-3 partly / overcast
  if (c >= 45 && c <= 48) return WX_FOG;           // fog / rime fog
  if (c >= 95) return WX_STORM;                    // 95-99 thunderstorm
  if ((c >= 71 && c <= 77) || c == 85 || c == 86)
    return WX_SNOW;                                // snowfall / snow showers
  if (c >= 51) return WX_RAIN;                     // drizzle/rain/showers
  return WX_CLOUDY;                                // anything odd: overcast
}

const char* Weather::kindLabel(WxKind k) {
  switch (k) {
    case WX_CLEAR:  return "Limpo";
    case WX_CLOUDY: return "Nublado";
    case WX_RAIN:   return "Chuva";
    case WX_STORM:  return "Tempestade";
    case WX_FOG:    return "Neblina";
    case WX_SNOW:   return "Neve";
  }
  return "";
}

// ---------- tiny JSON helpers (Open-Meteo replies only) ----------
// Find `"key":` inside [p, end) and return a pointer just past the colon.
static const char* findKey(const char* p, const char* end, const char* key) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* hit = strstr(p, pat);
  if (!hit || hit >= end) return nullptr;
  return hit + strlen(pat);
}

// Bound the object that starts at the '{' found right after `"name":`.
static bool findObject(const char* json, const char* name,
                       const char** out, const char** outEnd) {
  const char* p = findKey(json, json + strlen(json), name);
  if (!p || *p != '{') return false;
  int depth = 0;
  const char* q = p;
  for (; *q; q++) {
    if (*q == '{') depth++;
    else if (*q == '}' && --depth == 0) { *out = p; *outEnd = q; return true; }
  }
  return false;
}

// Read up to max numbers from `"key":[...]` inside [p, end).
static int readNumArray(const char* p, const char* end, const char* key,
                        float* out, int max) {
  const char* q = findKey(p, end, key);
  if (!q || *q != '[') return 0;
  q++;
  int n = 0;
  while (n < max && q < end && *q != ']') {
    char* after = nullptr;
    float v = strtof(q, &after);
    if (after == q) { q++; continue; }  // skip separators/nulls
    out[n++] = v;
    q = after;
    while (q < end && (*q == ',' || *q == ' ')) q++;
  }
  return n;
}

// Read up to max quoted strings from `"key":[...]` (ISO dates).
static int readStrArray(const char* p, const char* end, const char* key,
                        char out[][11], int max) {
  const char* q = findKey(p, end, key);
  if (!q || *q != '[') return 0;
  int n = 0;
  while (n < max && q < end && *q != ']') {
    const char* open = strchr(q, '"');
    if (!open || open >= end) break;
    const char* close = strchr(open + 1, '"');
    if (!close || close >= end) break;
    size_t len = close - open - 1;
    if (len > 10) len = 10;
    memcpy(out[n], open + 1, len);
    out[n][len] = 0;
    n++;
    q = close + 1;
  }
  return n;
}

// ISO "YYYY-MM-DD" -> weekday 0=Sun..6=Sat (civil days-from-epoch algorithm).
static int wdayFromIso(const char* iso) {
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;  // since 1970-01-01
  return (int)(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
}

static const char* const WDAY_PT[7] = {"Dom", "Seg", "Ter", "Qua",
                                       "Qui", "Sex", "Sab"};

// ---------- lifecycle ----------
void Weather::begin(bool (*busy)()) {
  _busy = busy;
  loadNvs();
  // Core 0 (with WiFi/lwIP), prio 2: below the audio reader (3). 8KB stack:
  // TLS handshake runs here.
  xTaskCreatePinnedToCore(taskTrampoline, "weather", 8192, this, 2, &_task, 0);
}

void Weather::loadNvs() {
  Preferences p;
  p.begin("wx", true);
  _lat = p.getFloat("lat", 0);
  _lon = p.getFloat("lon", 0);
  String c = p.getString("city", "");
  p.end();
  strlcpy(_city, c.c_str(), sizeof(_city));
  _hasLoc = _city[0] != 0;
}

void Weather::saveNvs() {
  Preferences p;
  p.begin("wx", false);
  p.putFloat("lat", _lat);
  p.putFloat("lon", _lon);
  p.putString("city", _city);
  p.end();
}

void Weather::setLocation(float lat, float lon, const char* city) {
  _lat = lat;
  _lon = lon;
  // ASCII-only for the display font (portal already transliterates; belt
  // and suspenders for serial/HA sources).
  size_t o = 0;
  for (const char* c = city; *c && o + 1 < sizeof(_city); c++)
    if ((uint8_t)*c >= 0x20 && (uint8_t)*c < 0x7F) _city[o++] = *c;
  _city[o] = 0;
  _hasLoc = _city[0] != 0;
  saveNvs();
  if (_hasLoc) requestFetch(0);
  else _lastOkMs = 0;  // cleared: back to dormant
}

void Weather::requestFetch(unsigned long maxAgeMs) {
  if (!_hasLoc) return;
  if (maxAgeMs > 0 && staleMs() < maxAgeMs) return;  // cache is fresh enough
  _fetchNow = true;
}

void Weather::loop() {
  if (!_stReady) return;
  _tempNow = _st.tempNow;
  _codeNow = _st.codeNow;
  memcpy(_days, _st.days, sizeof(_days));
  _dayCount = _st.dayCount;
  _lastOkMs = millis();
  _stReady = false;
}

// ---------- fetch task ----------
void Weather::taskTrampoline(void* arg) { static_cast<Weather*>(arg)->taskLoop(); }

void Weather::taskLoop() {
  unsigned long nextAt = 0;  // 0 = asap (boot fetch)
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const bool due = _fetchNow || (long)(millis() - nextAt) >= 0;
    if (!due) continue;
    if (!_hasLoc || WiFi.status() != WL_CONNECTED || _stReady ||
        (_busy && _busy())) {
      // Not ready (no location/WiFi, media streaming, or main hasn't adopted
      // the previous result yet): look again in a bit without burning the
      // failure backoff.
      if (!_fetchNow) nextAt = millis() + 15000;
      continue;
    }
    _fetchNow = false;
    const bool ok = fetchOnce();
    // Failure backoff: 5 min normally, but only 30 s while we've NEVER had
    // data (a boot fetch that races WiFi settling shouldn't leave the
    // forecast screen empty for 5 minutes).
    nextAt = millis() + (ok ? FETCH_OK_MS
                            : (_lastOkMs ? FETCH_FAIL_MS : 30000UL));
  }
}

bool Weather::fetchOnce() {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max"
           "&timezone=auto&forecast_days=%d",
           _lat, _lon, MAX_DAYS);

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // verified TLS (IDF CA bundle)
  cfg.user_agent = "HomeCritters";

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;

  bool ok = false;
  char* body = (char*)malloc(BODY_MAX);
  do {
    if (!body) break;
    if (esp_http_client_open(client, 0) != ESP_OK) break;
    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
      Serial.printf("[wx] http %d\n", status);
      break;
    }
    size_t got = 0;
    while (got < BODY_MAX - 1) {
      int r = esp_http_client_read(client, body + got, BODY_MAX - 1 - got);
      if (r <= 0) break;
      got += r;
    }
    body[got] = 0;
    if (got < 50) break;

    // --- current ---
    const char *cur, *curEnd;
    if (!findObject(body, "current", &cur, &curEnd)) break;
    float f[MAX_DAYS];
    if (readNumArray(cur, curEnd, "temperature_2m", f, 1) < 1) {
      // temperature_2m is a scalar, not an array - read it directly.
      const char* q = findKey(cur, curEnd, "temperature_2m");
      if (!q) break;
      f[0] = strtof(q, nullptr);
    }
    _st.tempNow = (int8_t)lroundf(f[0]);
    const char* q = findKey(cur, curEnd, "weather_code");
    if (!q) break;
    _st.codeNow = (uint8_t)atoi(q);

    // --- daily ---
    const char *dl, *dlEnd;
    if (!findObject(body, "daily", &dl, &dlEnd)) break;
    char dates[MAX_DAYS][11];
    float codes[MAX_DAYS], his[MAX_DAYS], los[MAX_DAYS], pops[MAX_DAYS];
    const int nd = readStrArray(dl, dlEnd, "time", dates, MAX_DAYS);
    const int nc = readNumArray(dl, dlEnd, "weather_code", codes, MAX_DAYS);
    const int nh = readNumArray(dl, dlEnd, "temperature_2m_max", his, MAX_DAYS);
    const int nl = readNumArray(dl, dlEnd, "temperature_2m_min", los, MAX_DAYS);
    const int np = readNumArray(dl, dlEnd, "precipitation_probability_max",
                                pops, MAX_DAYS);
    int n = min(min(nd, nc), min(nh, nl));
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      strlcpy(_st.days[i].day, WDAY_PT[wdayFromIso(dates[i])],
              sizeof(_st.days[i].day));
      _st.days[i].code = (uint8_t)lroundf(codes[i]);
      _st.days[i].hi = (int8_t)lroundf(his[i]);
      _st.days[i].lo = (int8_t)lroundf(los[i]);
      _st.days[i].pop = (i < np) ? (int8_t)lroundf(pops[i]) : -1;
    }
    _st.dayCount = n;
    _stReady = true;  // main adopts in loop()
    ok = true;
    Serial.printf("[wx] ok: now %d C code %d, %d days\n", _st.tempNow,
                  _st.codeNow, n);
  } while (false);

  free(body);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (!ok) Serial.println("[wx] fetch failed");
  return ok;
}
