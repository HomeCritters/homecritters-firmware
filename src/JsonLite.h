#pragma once
#include <Arduino.h>
#include <cstring>

// ============================================================
// JsonLite: tiny zero-allocation JSON scanning helpers, shared
// by the modules that parse small, known-shape JSON payloads
// (Weather: Open-Meteo replies; HaPanel: the plugin's compact
// ha:list/ha:upd frames). NOT a general JSON parser - inputs
// are ASCII and machine-generated (json.dumps / Open-Meteo).
// ============================================================

namespace jsonlite {

// Find `"key":` inside [p, end) and return a pointer just past the colon.
static inline const char* findKey(const char* p, const char* end,
                                  const char* key) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* hit = strstr(p, pat);
  if (!hit || hit >= end) return nullptr;
  return hit + strlen(pat);
}

// Bound the object that starts at the '{' found right after `"name":`.
static inline bool findObject(const char* json, const char* name,
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

// Extract a string value for "<key>" from a JSON object [obj, end). Handles
// the \" and \\ escapes json.dumps emits and skips \uXXXX (inputs are
// transliterated to ASCII upstream). Returns true if the key was found.
static inline bool getStr(const char* obj, const char* end, const char* key,
                          char* out, size_t outsz) {
  const char* p = findKey(obj, end, key);
  if (!p || *p != '"') return false;  // missing or not a string
  p++;
  size_t o = 0;
  while (p < end && *p != '"') {
    char c = *p;
    if (c == '\\' && p + 1 < end) {
      p++;
      if (*p == 'u') { p += 4; p++; continue; }  // skip \uXXXX (non-ASCII)
      c = *p;  // \" \\ \/ etc.
    }
    if (o + 1 < outsz) out[o++] = c;
    p++;
  }
  out[o] = 0;
  return true;
}

// true only if "<key>": true.
static inline bool getBool(const char* obj, const char* end, const char* key) {
  const char* p = findKey(obj, end, key);
  return p && strncmp(p, "true", 4) == 0;
}

// Read up to max numbers from `"key":[...]` inside [p, end).
static inline int readNumArray(const char* p, const char* end, const char* key,
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
static inline int readStrArray(const char* p, const char* end, const char* key,
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

// Split a JSON array into objects (brace-depth aware, quote/escape safe) and
// hand each [start, end) pair to `fn`.
template <typename F>
static inline void forEachObject(const char* json, F fn) {
  const char* p = json;
  while (*p && *p != '[') p++;
  if (*p) p++;
  while (*p) {
    while (*p && *p != '{') p++;
    if (!*p) break;
    const char* start = p;
    int depth = 0;
    bool inStr = false;
    for (; *p; p++) {
      const char c = *p;
      if (inStr) {
        if (c == '\\' && p[1]) p++;
        else if (c == '"') inStr = false;
      } else if (c == '"') {
        inStr = true;
      } else if (c == '{') {
        depth++;
      } else if (c == '}') {
        if (--depth == 0) { p++; break; }
      }
    }
    fn(start, p);  // [start, p) is one object
    while (*p == ',' || *p == ' ') p++;
    if (*p == ']') break;
  }
}

}  // namespace jsonlite
