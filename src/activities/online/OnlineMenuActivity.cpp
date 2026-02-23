#include "../../DebugConfig.h"
#include "OnlineMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "../../MappedInputManager.h"
#include "../../ScreenComponents.h"
#include "../../fontIds.h"

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
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
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

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 30, "ONLINE");

  // Draw menu items
  constexpr int itemHeight = 60;
  constexpr int margin = 20;
  constexpr int startY = 100;
  const int itemWidth = screenWidth - 2 * margin;

  for (size_t i = 0; i < menuItems.size(); i++) {
    const int y = startY + i * (itemHeight + 10);
    const bool selected = (i == static_cast<size_t>(selectedIndex));

    if (selected) {
      renderer.fillRect(margin, y, itemWidth, itemHeight);
    } else {
      renderer.drawRect(margin, y, itemWidth, itemHeight);
    }

    const char* name = menuItems[i].displayName.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, name);
    const int textX = margin + (itemWidth - textWidth) / 2;
    const int textY = y + (itemHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.drawText(UI_10_FONT_ID, textX, textY, name, !selected);
  }

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
