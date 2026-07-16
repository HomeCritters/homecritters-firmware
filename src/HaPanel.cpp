#include "HaPanel.h"
#include <cstring>

// Extract a string value for "<key>" from a JSON object [obj, end). Handles
// the \" and \\ escapes json.dumps emits (input is ASCII: the plugin
// transliterates names). Returns true if the key was found.
static bool jsonStr(const char* obj, const char* end, const char* key,
                    char* out, size_t outsz) {
  char pat[8];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(obj, pat);
  if (!p || p >= end) return false;
  p += strlen(pat);
  if (*p != '"') return false;  // expect a string
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

static bool jsonBool(const char* obj, const char* end, const char* key) {
  char pat[8];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(obj, pat);
  if (!p || p >= end) return false;
  return strncmp(p + strlen(pat), "true", 4) == 0;
}

bool HaPanel::parseOne(const char* obj, const char* objEnd, Entity& e) {
  memset(&e, 0, sizeof(e));
  if (!jsonStr(obj, objEnd, "id", e.id, sizeof(e.id))) return false;
  jsonStr(obj, objEnd, "n", e.name, sizeof(e.name));
  jsonStr(obj, objEnd, "d", e.domain, sizeof(e.domain));
  jsonStr(obj, objEnd, "s", e.state, sizeof(e.state));
  jsonStr(obj, objEnd, "v", e.value, sizeof(e.value));
  jsonStr(obj, objEnd, "dc", e.devclass, sizeof(e.devclass));
  e.controllable = jsonBool(obj, objEnd, "c");
  e.pending = false;
  return true;
}

// Split a JSON array into objects (brace-depth aware, quote/escape safe) and
// hand each to `fn`.
template <typename F>
static void forEachObject(const char* json, F fn) {
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

bool HaPanel::setList(const char* json) {
  if (!json) return false;
  _count = 0;
  forEachObject(json, [&](const char* o, const char* end) {
    if (_count >= MAX) return;
    Entity e;
    if (parseOne(o, end, e)) _items[_count++] = e;
  });
  _lastMs = millis();
  return true;
}

int HaPanel::indexOfId(const char* id) const {
  for (int i = 0; i < _count; i++)
    if (strcmp(_items[i].id, id) == 0) return i;
  return -1;
}

void HaPanel::applyUpdate(const char* json) {
  if (!json) return;
  // "ha:upd:" carries a single object (not an array); parse it directly.
  const char* o = strchr(json, '{');
  Entity e;
  if (!o || !parseOne(o, o + strlen(o), e)) return;
  const int idx = indexOfId(e.id);
  if (idx >= 0) {
    _items[idx] = e;
  } else if (_count < MAX) {
    _items[_count++] = e;
  }
  _lastMs = millis();
}
