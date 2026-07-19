#pragma once
#include <Arduino.h>

// ============================================================
// TaskRegistry: tiny roster of the firmware's long-lived tasks
// for the serial `top` report. The Arduino core ships FreeRTOS
// without the trace facility (no uxTaskGetSystemState), so each
// creation site registers itself here instead - stack high-water
// and state still come live from the handle.
// (Function-local statics: one shared instance across TUs, C++11.)
// ============================================================

namespace taskreg {

struct Entry {
  const char* name;
  TaskHandle_t handle;
  int8_t core;   // -1 = no affinity
  uint8_t prio;
};

static constexpr int MAX_TASKS = 12;

inline Entry* tasks() {
  static Entry t[MAX_TASKS];
  return t;
}
inline int& count() {
  static int c = 0;
  return c;
}

inline void add(const char* name, TaskHandle_t h, int8_t core, uint8_t prio) {
  if (!h || count() >= MAX_TASKS) return;
  tasks()[count()++] = {name, h, core, prio};
}

}  // namespace taskreg
