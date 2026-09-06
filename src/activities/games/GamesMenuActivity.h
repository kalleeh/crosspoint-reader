#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../UiListActivity.h"

class GamesMenuActivity final : public UiListActivity {
 private:
  struct GameEntry {
    std::string name;
    std::string displayName;
    std::function<void()> onSelect;
  };

  std::vector<GameEntry> games;
  // Row buffer aliasing games[i].displayName; built once in onEnter (games are
  // all registered before the activity is entered).
  std::vector<freeink::ui::ListItem> rowItems;
  const std::function<void()> onBack;

  int listCount() const override { return static_cast<int>(games.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::function<void()>& onBack)
      : UiListActivity("Games", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;

  // Register a game in the menu
  void registerGame(const std::string& name, const std::string& displayName, const std::function<void()>& onSelect);
};
