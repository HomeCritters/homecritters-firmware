#include "HaPanel.h"
#include <cstring>
#include "JsonLite.h"

bool HaPanel::parseOne(const char* obj, const char* objEnd, Entity& e) {
  using namespace jsonlite;
  memset(&e, 0, sizeof(e));
  if (!getStr(obj, objEnd, "id", e.id, sizeof(e.id))) return false;
  getStr(obj, objEnd, "n", e.name, sizeof(e.name));
  getStr(obj, objEnd, "d", e.domain, sizeof(e.domain));
  getStr(obj, objEnd, "s", e.state, sizeof(e.state));
  getStr(obj, objEnd, "v", e.value, sizeof(e.value));
  getStr(obj, objEnd, "dc", e.devclass, sizeof(e.devclass));
  e.controllable = getBool(obj, objEnd, "c");
  e.pending = false;
  return true;
}

bool HaPanel::setList(const char* json) {
  if (!json) return false;
  _count = 0;
  jsonlite::forEachObject(json, [&](const char* o, const char* end) {
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
