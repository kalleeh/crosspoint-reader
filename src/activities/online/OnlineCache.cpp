#include "OnlineCache.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

namespace {
constexpr char CACHE_FILE[] = "/.crosspoint/online-cache.json";

// Epoch seconds if the wall clock is synced (NTP/RTC), else 0
uint32_t nowEpoch() {
  time_t now = time(nullptr);
  return now > 1000000000L ? (uint32_t)now : 0;
}

bool loadDoc(JsonDocument& doc) {
  if (!Storage.exists(CACHE_FILE)) return false;
  String json = Storage.readFile(CACHE_FILE);
  if (json.isEmpty()) return false;
  if (deserializeJson(doc, json)) {
    LOG_ERR("OCACHE", "JSON parse error");
    return false;
  }
  return true;
}

bool saveDoc(const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(CACHE_FILE, json);
}
}  // namespace

namespace OnlineCache {

void saveWeather(const char* location, const char* condition, int temperature) {
  JsonDocument doc;
  loadDoc(doc);  // keep the other section intact; empty doc if none

  // Value-change guard: skip the SD write when nothing changed — unless the
  // clock is synced, in which case fetchedAt is the payload (the sleep band
  // reports freshness from it) and must advance even for an identical reading.
  const uint32_t now = nowEpoch();
  if (now == 0 && doc["weather"]["location"] == location && doc["weather"]["condition"] == condition &&
      doc["weather"]["temp"] == temperature) {
    return;
  }

  doc["weather"]["location"] = location;
  doc["weather"]["condition"] = condition;
  doc["weather"]["temp"] = temperature;
  doc["weather"]["fetchedAt"] = now;
  saveDoc(doc);
}

void saveWord(const char* word) {
  JsonDocument doc;
  loadDoc(doc);

  // Same rule as saveWeather: an identical word still advances fetchedAt when
  // the clock is synced, so the daily refresh gate can see it was checked.
  const uint32_t now = nowEpoch();
  if (now == 0 && doc["word"]["word"] == word) return;

  doc["word"]["word"] = word;
  doc["word"]["fetchedAt"] = now;
  saveDoc(doc);
}

CachedWeather loadWeather() {
  CachedWeather w;
  JsonDocument doc;
  if (!loadDoc(doc) || doc["weather"]["location"].isNull()) return w;

  strlcpy(w.location, doc["weather"]["location"] | "", sizeof(w.location));
  strlcpy(w.condition, doc["weather"]["condition"] | "", sizeof(w.condition));
  w.temperature = doc["weather"]["temp"] | 0;
  w.fetchedAtEpoch = doc["weather"]["fetchedAt"] | 0;
  w.valid = w.location[0] != '\0';
  return w;
}

CachedWord loadWord() {
  CachedWord c;
  JsonDocument doc;
  if (!loadDoc(doc) || doc["word"]["word"].isNull()) return c;

  strlcpy(c.word, doc["word"]["word"] | "", sizeof(c.word));
  c.fetchedAtEpoch = doc["word"]["fetchedAt"] | 0;
  c.valid = c.word[0] != '\0';
  return c;
}

}  // namespace OnlineCache
