#include "ForkSettings.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

// Initialize the static instance
ForkSettings ForkSettings::instance;

namespace {
constexpr char FORK_SETTINGS_FILE[] = "/.crosspoint/fork-settings.json";
}  // namespace

bool ForkSettings::loadFromFile() {
  if (!Storage.exists(FORK_SETTINGS_FILE)) {
    return false;
  }

  String json = Storage.readFile(FORK_SETTINGS_FILE);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("FST", "JSON parse error: %s", error.c_str());
    return false;
  }

  showHiddenFiles = doc["showHiddenFiles"] | (uint8_t)0;

  LOG_DBG("FST", "Fork settings loaded from file");
  return true;
}

bool ForkSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["showHiddenFiles"] = showHiddenFiles;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(FORK_SETTINGS_FILE, json);
}
