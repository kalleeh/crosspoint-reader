#pragma once

// Refreshes the SD-cached weather (and the wall clock via NTP) on the way into
// deep sleep so the sleep-screen band shows a current reading. Runs before the
// sleep screen renders; the panel keeps showing the last page meanwhile.
//
// The X4 cannot refresh *during* sleep: its "deep sleep" on battery drives the
// battery MOSFET off (HalPowerManager::startDeepSleep), so the chip is fully
// unpowered until the power button. Refreshing at sleep entry is the only
// option, so it is rate-limited by FORK_SETTINGS.sleepWeatherRefresh and
// skipped on low battery / low heap / no saved WiFi.
namespace SleepInfoRefresh {

// Returns true when a fresh weather reading was fetched and cached.
bool run(bool fromTimeout);

}  // namespace SleepInfoRefresh
