#pragma once

// ============================================================
// NetBench: one-shot network throughput probes (debug console).
//
// Measures the device's REAL sustained TCP pull rate, isolated
// from the audio pipeline - the number that decides which media
// format Music Assistant should send (plan gate G0).
//
//   nb:<url>   raw WiFiClient GET, discard body    -> raw ceiling
//   nb2:<url>  esp_http_client, same discard loop  -> what the
//              future AudioReader will see (same client config),
//              and proof the IDF client works in this build
//   mem        heap / PSRAM report
//
// Runs on a temporary core-0 prio-3 task (where the reader task
// will live), reports per-second KB/s + avg/min/max to Serial.
// ============================================================

namespace netbench {

// Start a benchmark (async; result lines go to Serial). Ignored with a
// warning if one is already running. url must start with http://
void runRaw(const char* url);
void runIdf(const char* url);

void memReport();
void cpuReport();  // 3s prio-0 counter per core: relative idle CPU
// Linux-top style health snapshot. topSample BLOCKS ~1.1s (counter tasks) -
// never call from the render loop; the console task and the WS top task are
// the intended callers.
struct TopSnap {
  int busy0, busy1;
  unsigned long upSec;
  unsigned heapK, heapMinK, heapBigK, psramK;
  unsigned heapTotK, psramTotK;  // capacity (usage bars in the portal)
  float tempC;                   // SoC internal temperature sensor
};
void topSample(TopSnap& s);
void topReport();                          // human format -> Serial
void topJson(char* out, unsigned int n);   // "top:{...}" -> portal dev panel
void wifiPsReport();  // print + force WiFi power save NONE

}  // namespace netbench
