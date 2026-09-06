#pragma once
#include <cstdint>

/**
 * Fork-specific settings that are not part of upstream CrossPointSettings.
 * Stored separately in /.crosspoint/fork-settings.json to avoid polluting
 * the upstream settings binary format.
 */
class ForkSettings {
 public:
  static ForkSettings& getInstance() { return instance; }

  // Show hidden files (starting with '.') in the file browser
  uint8_t showHiddenFiles = 0;

  // Overlay cached weather/word-of-day on the sleep screen (default on)
  uint8_t sleepInfoOverlay = 1;

  // Refresh the cached weather over WiFi on the way into sleep (see
  // SleepInfoRefresh). The X4 is powered off while "asleep", so this is the
  // only moment the band can be updated; keep it rare to protect the battery.
  enum SLEEP_REFRESH : uint8_t { SLEEP_REFRESH_OFF = 0, SLEEP_REFRESH_TIMEOUT_ONLY = 1, SLEEP_REFRESH_EVERY = 2 };
  uint8_t sleepWeatherRefresh = SLEEP_REFRESH_TIMEOUT_ONLY;

  bool loadFromFile();
  bool saveToFile() const;

 private:
  ForkSettings() = default;
  static ForkSettings instance;
};

#define FORK_SETTINGS ForkSettings::getInstance()
