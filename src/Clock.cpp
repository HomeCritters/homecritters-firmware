#include "Clock.h"
#include <time.h>

static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.google.com";
static const char* NTP3 = "time.cloudflare.com";

void Clock::begin() {
  _prefs.begin("clock", false);  // RW: creates the namespace on first boot
  // Migration to the new defaults (disabled + timezone discovered by the
  // browser). Runs once on devices carrying the old saved config.
  if ((_prefs.isKey("ver") ? _prefs.getInt("ver") : 0) < 2) {
    _prefs.putBool("en", false);
    _prefs.putString("tz", "");
    _prefs.putInt("ver", 2);
  }
  // Read only existing keys (avoids NVS "NOT_FOUND" logs); else write defaults.
  if (_prefs.isKey("en"))   _enabled = _prefs.getBool("en");   else _prefs.putBool("en", _enabled);
  if (_prefs.isKey("tz"))   _tz = _prefs.getString("tz");      else _prefs.putString("tz", _tz);
  if (_prefs.isKey("idle")) _idleSec = _prefs.getInt("idle");  else _prefs.putInt("idle", _idleSec);
  if (_prefs.isKey("menu")) _menuSec = _prefs.getInt("menu");  else _prefs.putInt("menu", _menuSec);
  if (_prefs.isKey("h24"))  _h24 = _prefs.getBool("h24");      else _prefs.putBool("h24", _h24);
  if (_prefs.isKey("dmy"))  _dmy = _prefs.getBool("dmy");      else _prefs.putBool("dmy", _dmy);
  _prefs.end();
  if (tzKnown()) { setenv("TZ", _tz.c_str(), 1); tzset(); }
}

void Clock::startNtp() {
  // Applies the TZ and kicks off SNTP with 3 fallback servers.
  configTzTime(_tz.c_str(), NTP1, NTP2, NTP3);
  _sntpStarted = true;
}

void Clock::update(bool wifiConnected) {
  // Only sync when the timezone is known (time is meaningless otherwise).
  if (wifiConnected && !_sntpStarted && tzKnown()) startNtp();
  if (!_synced && _sntpStarted && time(nullptr) > 1700000000) {
    _synced = true;
  }
}

int Clock::localHour() const {
  if (!_synced) return -1;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour;
}

void Clock::setEnabled(bool e) {
  _enabled = e;
  _prefs.begin("clock", false);
  _prefs.putBool("en", e);
  _prefs.end();
}

void Clock::setIdleSec(int s) {
  if (s < 5) s = 5;
  if (s > 3600) s = 3600;
  _idleSec = s;
  _prefs.begin("clock", false);
  _prefs.putInt("idle", s);
  _prefs.end();
}

void Clock::setMenuTimeoutSec(int s) {
  if (s < 0) s = 0;
  if (s > 3600) s = 3600;
  _menuSec = s;
  _prefs.begin("clock", false);
  _prefs.putInt("menu", s);
  _prefs.end();
}

void Clock::setH24(bool v) {
  _h24 = v;
  _prefs.begin("clock", false);
  _prefs.putBool("h24", v);
  _prefs.end();
}

void Clock::setDateDmy(bool v) {
  _dmy = v;
  _prefs.begin("clock", false);
  _prefs.putBool("dmy", v);
  _prefs.end();
}

void Clock::setTz(const String& tz) {
  // Bound + sanitize before this reaches setenv(): POSIX TZ strings are short
  // printable ASCII; the raw value arrives from the network (WS `tz:` command).
  if (tz.length() > 64) return;
  for (size_t i = 0; i < tz.length(); i++) {
    const char c = tz[i];
    if (c < 0x21 || c > 0x7E) return;  // no spaces/control/8-bit chars
  }
  _tz = tz;
  _prefs.begin("clock", false);
  _prefs.putString("tz", tz);
  _prefs.end();
  setenv("TZ", _tz.c_str(), 1);
  tzset();
  if (_sntpStarted) configTzTime(_tz.c_str(), NTP1, NTP2, NTP3);
}

void Clock::format(char* timeStr, size_t tn, char* dateStr, size_t dn) const {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  const char colon = (lt.tm_sec % 2) ? ' ' : ':';  // blink every second
  char date[12];
  strftime(date, sizeof(date), _dmy ? "%d/%m/%Y" : "%m/%d/%Y", &lt);
  if (_h24) {
    snprintf(timeStr, tn, "%02d%c%02d", lt.tm_hour, colon, lt.tm_min);
    snprintf(dateStr, dn, "%s", date);
  } else {
    int h12 = lt.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(timeStr, tn, "%d%c%02d", h12, colon, lt.tm_min);
    snprintf(dateStr, dn, "%s  %s", lt.tm_hour < 12 ? "AM" : "PM", date);
  }
}
