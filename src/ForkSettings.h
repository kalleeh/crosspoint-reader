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

  bool loadFromFile();
  bool saveToFile() const;

 private:
  ForkSettings() = default;
  static ForkSettings instance;
};

#define FORK_SETTINGS ForkSettings::getInstance()
