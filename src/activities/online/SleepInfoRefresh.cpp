#include "SleepInfoRefresh.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>

#include <ctime>

#include "../../ForkSettings.h"
#include "../../WifiCredentialStore.h"
#include "OnlineCache.h"
#include "OnlineContentFetcher.h"

namespace {
// Below this the refresh is skipped: a WiFi + TLS burst on a near-empty
// battery is the wrong trade for a nicer sleep screen.
constexpr uint16_t MIN_BATTERY_PERCENT = 20;
// wttr.in is HTTPS; a TLS handshake needs a contiguous ~40KB. Leaving the
// reader can sit well below that, in which case just keep the cached value.
constexpr uint32_t MIN_FREE_HEAP = 90000;
// Refresh the word at most once a day (20h so a slightly earlier bedtime still counts).
constexpr uint32_t WORD_MAX_AGE_SEC = 20UL * 3600;

}  // namespace

namespace SleepInfoRefresh {

bool run(const bool fromTimeout) {
  if (!FORK_SETTINGS.sleepInfoOverlay) return false;
  const uint8_t mode = FORK_SETTINGS.sleepWeatherRefresh;
  if (mode == ForkSettings::SLEEP_REFRESH_OFF) return false;
  if (mode == ForkSettings::SLEEP_REFRESH_TIMEOUT_ONLY && !fromTimeout) return false;

  const uint16_t battery = powerManager.getBatteryPercentage();
  if (battery < MIN_BATTERY_PERCENT) {
    LOG_INF("SLPR", "Skip: battery %u%%", battery);
    return false;
  }
  if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
    LOG_INF("SLPR", "Skip: free heap %u", (unsigned)ESP.getFreeHeap());
    return false;
  }
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() == 0) {
    LOG_INF("SLPR", "Skip: no saved WiFi");
    return false;
  }

  const unsigned long start = millis();
  bool fetched = false;
  if (OnlineContentFetcher::ensureWiFi()) {
    OnlineContentFetcher::syncWallClock();
    fetched = OnlineContentFetcher::fetchWeather(false).success;  // persists to OnlineCache

    // Word of the day: a random word + dictionary lookup (two more requests),
    // so only once a day. Needs the clock to know the cached word's age.
    if (OnlineContentFetcher::wallClockKnown()) {
      const auto word = OnlineCache::loadWord();
      const uint32_t nowEpoch = static_cast<uint32_t>(time(nullptr));
      const bool wordDue = !word.valid || word.fetchedAtEpoch == 0 || nowEpoch - word.fetchedAtEpoch > WORD_MAX_AGE_SEC;
      if (wordDue) {
        const bool wordOk = OnlineContentFetcher::fetchWordOfDay().success;  // persists to OnlineCache
        LOG_INF("SLPR", "Word refresh %s", wordOk ? "ok" : "failed");
      }
    }
  }
  // Drop the radio right away rather than at the very end of enterDeepSleep().
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_INF("SLPR", "Weather refresh %s in %lu ms", fetched ? "ok" : "failed", millis() - start);
  return fetched;
}

}  // namespace SleepInfoRefresh
