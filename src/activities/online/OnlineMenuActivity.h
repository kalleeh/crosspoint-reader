#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class OnlineMenuActivity final : public Activity {
 private:
  struct MenuItem {
    std::string name;
    std::string displayName;
    std::function<void()> onSelect;
  };

  std::vector<MenuItem> menuItems;
  int selectedIndex;
  const std::function<void()> onBack;

  void render();

 public:
  explicit OnlineMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onBack)
      : Activity("Online", renderer, mappedInput), onBack(onBack), selectedIndex(0) {}

  void registerItem(const std::string& name, const std::string& displayName,
                    const std::function<void()>& onSelect);

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
