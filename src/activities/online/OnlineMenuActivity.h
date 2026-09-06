#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../UiListActivity.h"

class OnlineMenuActivity final : public UiListActivity {
 private:
  struct MenuItem {
    std::string name;
    std::string displayName;
    std::function<void()> onSelect;
  };

  std::vector<MenuItem> menuItems;
  // Row buffer aliasing menuItems[i].displayName; built once in onEnter (items
  // are all registered before the activity is entered).
  std::vector<freeink::ui::ListItem> rowItems;
  const std::function<void()> onBack;

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit OnlineMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack)
      : UiListActivity("Online", renderer, mappedInput), onBack(onBack) {}

  void registerItem(const std::string& name, const std::string& displayName, const std::function<void()>& onSelect);

  void onEnter() override;
  void onExit() override;
};
