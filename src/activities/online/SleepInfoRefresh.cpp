#include "SleepInfoRefresh.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <ctime>

#include "../../ForkSettings.h"
#include "../../WifiCredentialStore.h"
#include "OnlineContentFetcher.h"

namespace {
// Below this the refresh is skipped: a WiFi + TLS burst on a near-empty
// battery is the wrong trade for a nicer sleep screen.
constexpr uint16_t MIN_BATTERY_PERCENT = 20;
// wttr.in is HTTPS; a TLS handshake needs a contiguous ~40KB. Leaving the
// reader can sit well below that, in which case just keep the cached value.
constexpr uint32_t MIN_FREE_HEAP = 90000;
constexpr unsigned long NTP_WAIT_MS = 4000;

bool wallClockKnown() { return time(nullptr) > 1000000000L; }

// Set the system clock from NTP so the cached reading gets a real timestamp.
// HalClock::syncFromNTP() is RTC-only (returns false on the X4), so poll SNTP here.
void syncWallClock() {
  if (wallClockKnown()) return;
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  const unsigned long start = millis();
  while (millis() - start < NTP_WAIT_MS) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || wallClockKnown()) break;
    delay(100);
  }
  LOG_INF("SLPR", "NTP %s", wallClockKnown() ? "synced" : "not synced");
}
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
    syncWallClock();
    fetched = OnlineContentFetcher::fetchWeather(false).success;  // persists to OnlineCache
  }
  // Drop the radio right away rather than at the very end of enterDeepSleep().
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_INF("SLPR", "Weather refresh %s in %lu ms", fetched ? "ok" : "failed", millis() - start);
  return fetched;
}

}  // namespace SleepInfoRefresh
