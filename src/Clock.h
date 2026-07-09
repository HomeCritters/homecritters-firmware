#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ============================================================
// Clock: NTP-backed wall clock. Syncs via SNTP once WiFi is up,
// applies the timezone (POSIX TZ string) and formats the time.
// Configurable (on/off, timezone, 12/24h, idle timeout) and
// persisted to NVS. The "clock mode" (showing it on screen) is
// decided in main from idle time; this class only owns the time.
// ============================================================

class Clock {
 public:
  void begin();
  void update(bool wifiConnected);  // starts/checks SNTP
  bool synced() const { return _synced; }

  bool enabled() const { return _enabled; }
  void setEnabled(bool e);
  const String& tz() const { return _tz; }
  bool tzKnown() const { return _tz.length() > 0; }
  void setTz(const String& tz);
  int idleSec() const { return _idleSec; }
  void setIdleSec(int s);
  bool h24() const { return _h24; }
  void setH24(bool v);

  // Fills "HH:MM" (with a blinking ':') and the date "[AM/PM] DD/MM/YYYY".
  void format(char* timeStr, size_t tn, char* dateStr, size_t dn) const;

 private:
  // Defaults: DISABLED with no timezone (unknown) - the browser detects
  // the timezone and enables it. The device cannot know it on its own.
  bool _enabled = false;
  String _tz = "";
  int _idleSec = 30;
  bool _h24 = true;
  bool _sntpStarted = false;
  bool _synced = false;
  Preferences _prefs;

  void startNtp();
};
