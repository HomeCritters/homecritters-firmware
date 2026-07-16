#pragma once
#include <Arduino.h>

// ============================================================
// HaPanel: the Home Assistant control panel model.
//
// The HA plugin (a WS client) pushes the exposed entities as
// "ha:list:<json>" (full) and "ha:upd:<json>" (one entity);
// WebPortal parses them into this table. The device screen
// (SCREEN_HA) renders it and taps send "ha:cmd:<id>:toggle"
// back. Everything runs on the render loop (WS is single-
// threaded there), so no locking.
// ============================================================

class HaPanel {
 public:
  static constexpr int MAX = 24;      // exposed entities cap
  struct Entity {
    char id[40];        // entity_id (for commands)
    char name[19];      // short friendly name
    char domain[16];    // light/switch/binary_sensor/... (longest + NUL)
    char state[13];     // raw state ("on"/"off"/value)
    char value[13];     // formatted value+unit (sensors)
    char devclass[13];  // HA device_class (motion/illuminance/... -> icon)
    bool controllable;  // tappable on/off
    bool pending;       // optimistic toggle awaiting confirmation
  };

  // Replace the whole list from a "ha:list:" JSON array. Returns false on
  // a parse problem (list left unchanged).
  bool setList(const char* json);
  // Apply one "ha:upd:" entity object (adds it if new).
  void applyUpdate(const char* json);
  void clear() { _count = 0; }

  int count() const { return _count; }
  const Entity& at(int i) const { return _items[i]; }
  Entity& at(int i) { return _items[i]; }
  int indexOfId(const char* id) const;

  // Freshness: ms since the last list/update (for a "disconnected" hint).
  unsigned long staleMs() const { return _lastMs ? millis() - _lastMs : 0xFFFFFFFF; }
  bool everReceived() const { return _lastMs != 0; }

 private:
  Entity _items[MAX] = {};
  int _count = 0;
  unsigned long _lastMs = 0;
  // Fill one Entity from a JSON object substring [obj, objEnd).
  bool parseOne(const char* obj, const char* objEnd, Entity& e);
};
