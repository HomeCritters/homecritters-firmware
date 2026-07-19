#pragma once
#include <Arduino.h>
#include <atomic>

// ============================================================
// Weather: standalone real-weather client (NO Home Assistant).
//
// Fetches current conditions + a 5-day daily forecast from the
// free Open-Meteo API (verified TLS via the IDF cert bundle) on
// its own core-0 task, on a fixed cadence:
//   - boot (once WiFi + a location exist): fetch now
//   - normal: every 30 min; on failure retry in 5 min
//   - setLocation() / requestFetch(): fetch now
// The task parses into a staging copy; the render loop adopts it
// via loop() (same handoff pattern as the screenshot flags).
//
// Location (lat/lon/city) persists in NVS ("wx"). It is set from
// the portal (browser does the geocoding), serial, or - as an
// optional convenience - pushed once by the HA plugin when the
// device reports no city. No location = feature dormant.
// ============================================================

// Coarse condition for the scene renderer (from WMO weather codes).
enum WxKind : uint8_t {
  WX_CLEAR = 0, WX_CLOUDY, WX_RAIN, WX_STORM, WX_FOG, WX_SNOW
};

class Weather {
 public:
  static constexpr int MAX_DAYS = 5;
  struct Day {
    char day[5];    // "Seg", "Ter", ... (ASCII, from the ISO date)
    uint8_t code;   // WMO weather code
    int8_t hi, lo;  // daily max/min, rounded
    int8_t pop;     // precipitation probability %
  };

  // busy() gate: fetches are skipped while it returns true (media streaming
  // owns the radio); the retry cadence picks the fetch up later.
  void begin(bool (*busy)() = nullptr);

  // Set/replace the location (NVS) and fetch right away. city is stored
  // ASCII-only (the display font); pass "" to clear the location entirely.
  void setLocation(float lat, float lon, const char* city);
  bool hasLocation() const { return _hasLoc; }
  const char* city() const { return _city; }

  // Fetch now if the current data is older than maxAgeMs (0 = always).
  void requestFetch(unsigned long maxAgeMs = 0);

  // Main-thread pump: adopts a finished fetch from the task's staging copy.
  void loop();

  bool everFetched() const { return _lastOkMs != 0; }
  // ms since the last successful fetch (max value if never).
  unsigned long staleMs() const {
    return _lastOkMs ? millis() - _lastOkMs : 0xFFFFFFFF;
  }
  // Data older than 3h = degrade the scene back to CLEAR (API/WiFi down).
  bool fresh() const { return staleMs() < 3UL * 60 * 60 * 1000; }

  WxKind kindNow() const { return fresh() ? kindFromCode(_codeNow) : WX_CLEAR; }
  int tempNow() const { return _tempNow; }
  int humNow() const { return _humNow; }  // relative humidity % (-1 unknown)
  uint8_t codeNow() const { return _codeNow; }
  int dayCount() const { return _dayCount; }
  const Day& day(int i) const { return _days[i]; }

  static WxKind kindFromCode(uint8_t code);
  static const char* kindLabel(WxKind k);  // PT label (coarse family)
  // Granular views of a WMO code (each variant gets its own visual):
  static uint8_t intensityFromCode(uint8_t c);  // 0 light, 1 normal, 2 heavy
  static bool codeFreezing(uint8_t c);          // 56/57/66/67 freezing rain
  static bool codeHail(uint8_t c);              // 96/99 thunderstorm w/ hail
  static const char* labelForCode(uint8_t c);   // PT label, granular
  // Debug helper: "rainy"/"hail"/... or a raw number -> WMO code; ""/unknown
  // -> -1 (= use the real weather). Shared by the serial console and the
  // portal's dev panel.
  static int codeFromName(const char* n);

 private:
  static void taskTrampoline(void* arg);
  void taskLoop();
  bool fetchOnce();  // blocking HTTPS GET + parse into staging (task only)
  void loadNvs();
  void saveNvs();

  // --- location (NVS) ---
  // Written by the render thread (setLocation), read by the core-0 fetch task.
  // Atomics with release/acquire ordering: _hasLoc is stored LAST (release) so
  // when the task sees it true, the lat/lon it then loads are the new pair - a
  // plain bool gave no such guarantee across cores (torn-location fetches).
  // _city stays plain: it's only touched on the render thread (display/NVS).
  std::atomic<bool> _hasLoc{false};
  std::atomic<float> _lat{0}, _lon{0};
  char _city[25] = {0};

  // --- adopted model (main thread reads) ---
  int8_t _tempNow = 0;
  int8_t _humNow = -1;
  uint8_t _codeNow = 0;
  Day _days[MAX_DAYS] = {};
  int _dayCount = 0;
  unsigned long _lastOkMs = 0;

  // --- staging (task writes, loop() adopts) ---
  struct Staging {
    int8_t tempNow;
    int8_t humNow;
    uint8_t codeNow;
    Day days[MAX_DAYS];
    int dayCount;
  };
  Staging _st = {};
  // task -> main handoff: the task fills _st, THEN stores _stReady (release);
  // loop() loads it (acquire) before copying, so the payload writes are
  // visible on the other core. volatile alone is not a memory barrier.
  std::atomic<bool> _stReady{false};
  std::atomic<bool> _fetchNow{false};  // main -> task "fetch asap" (flag only)
  bool (*_busy)() = nullptr;
  TaskHandle_t _task = nullptr;
};
