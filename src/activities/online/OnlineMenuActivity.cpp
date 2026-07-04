#include "../../DebugConfig.h"
#include "OnlineMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <ForkI18n.h>

#include "../../MappedInputManager.h"
#include "components/UITheme.h"

void OnlineMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  render();
}

void OnlineMenuActivity::onExit() { Activity::onExit(); }

void OnlineMenuActivity::registerItem(const std::string& name, const std::string& displayName,
                                       const std::function<void()>& onSelect) {
  menuItems.push_back({name, displayName, onSelect});
}

void OnlineMenuActivity::loop() {
  // Back on press-edge, consistent across all fork activities: mixing press
  // and release edges makes one physical press navigate two levels up.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  bool needsRedraw = false;

  // Check both Up/Left and Down/Right for navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      needsRedraw = true;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selectedIndex < static_cast<int>(menuItems.size()) - 1) {
      selectedIndex++;
      needsRedraw = true;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
      if (menuItems[selectedIndex].onSelect) {
        menuItems[selectedIndex].onSelect();
      }
    }
    return;
  }

  if (needsRedraw) {
    render();
  }
}

void OnlineMenuActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, fork_tr(STR_ONLINE_MENU_TITLE));

  // List
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screenHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, screenWidth, contentHeight},
      static_cast<int>(menuItems.size()), selectedIndex,
      [this](int index) { return menuItems[index].displayName; });

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
