#pragma once

#include <string>
#include <vector>

#include "../UiListActivity.h"

class OnlineMenuActivity final : public UiListActivity {
 private:
  struct MenuItem {
    std::string displayName;
    void (*onSelect)();
  };

  std::vector<MenuItem> menuItems;
  // Row buffer aliasing menuItems[i].displayName; built once in onEnter (items
  // are all registered before the activity is entered).
  std::vector<freeink::ui::ListItem> rowItems;
  void (*const onBack)();

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit OnlineMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*onBack)())
      : UiListActivity("Online", renderer, mappedInput), onBack(onBack) {}

  void registerItem(const std::string& displayName, void (*onSelect)());

  void onEnter() override;
  void onExit() override;
};
