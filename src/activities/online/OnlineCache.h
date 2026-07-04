#pragma once
#include <cstdint>

// SD-persisted cache of the last successful weather and word-of-the-day
// fetches. The in-RAM caches in OnlineContentFetcher are lost at deep sleep
// (chip reset), so the sleep screen reads this file instead.
// Stored as JSON at /.crosspoint/online-cache.json.
namespace OnlineCache {

struct CachedWeather {
  bool valid = false;
  char location[64] = "";
  char condition[32] = "";
  int temperature = 0;
  uint32_t fetchedAtEpoch = 0;  // 0 = wall clock was not synced at fetch time
};

struct CachedWord {
  bool valid = false;
  char word[48] = "";
  uint32_t fetchedAtEpoch = 0;
};

// Persist a successful fetch. Skips the SD write when the content is
// unchanged (value-change guard per the SPIFFS/SD write-throttling rule).
void saveWeather(const char* location, const char* condition, int temperature);
void saveWord(const char* word);

// Load from SD. Returns structs with valid=false if no cache exists.
CachedWeather loadWeather();
CachedWord loadWord();

}  // namespace OnlineCache
