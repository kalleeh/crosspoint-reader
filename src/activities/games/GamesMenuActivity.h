#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class GamesMenuActivity final : public Activity {
 private:
  struct GameEntry {
    std::string name;
    std::string displayName;
    std::function<void()> onSelect;
  };

  std::vector<GameEntry> games;
  int selectedIndex;
  const std::function<void()> onBack;

  void render();

 public:
  explicit GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack)
      : Activity("Games", renderer, mappedInput), selectedIndex(0), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

  // Register a game in the menu
  void registerGame(const std::string& name, const std::string& displayName, const std::function<void()>& onSelect);
};
