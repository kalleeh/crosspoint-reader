#include "GamesMenuActivity.h"

#include <GfxRenderer.h>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
}

void GamesMenuActivity::onExit() { Activity::onExit(); }

void GamesMenuActivity::registerGame(const std::string& name, const std::string& displayName,
                                      const std::function<void()>& onSelect) {
  games.push_back({name, displayName, onSelect});
}

void GamesMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  bool needsRedraw = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      needsRedraw = true;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selectedIndex < static_cast<int>(games.size()) - 1) {
      selectedIndex++;
      needsRedraw = true;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(games.size())) {
      if (games[selectedIndex].onSelect) {
        games[selectedIndex].onSelect();
      }
    }
    return;
  }

  if (needsRedraw) {
    render();
  }
}

void GamesMenuActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 30, "GAMES");

  // Draw game list
  constexpr int itemHeight = 50;
  constexpr int margin = 20;
  constexpr int startY = 80;
  const int itemWidth = screenWidth - 2 * margin;

  // Calculate visible range (show max 10 items at once)
  constexpr int maxVisible = 10;
  int startIdx = selectedIndex - maxVisible / 2;
  if (startIdx < 0)
    startIdx = 0;
  int endIdx = startIdx + maxVisible;
  if (endIdx > static_cast<int>(games.size()))
    endIdx = games.size();
  if (endIdx - startIdx < maxVisible && startIdx > 0) {
    startIdx = endIdx - maxVisible;
    if (startIdx < 0)
      startIdx = 0;
  }

  for (int i = startIdx; i < endIdx; i++) {
    const int y = startY + (i - startIdx) * (itemHeight + 5);
    const bool selected = (i == selectedIndex);

    if (selected) {
      renderer.fillRect(margin, y, itemWidth, itemHeight);
    } else {
      renderer.drawRect(margin, y, itemWidth, itemHeight);
    }

    const char* name = games[i].displayName.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, name);
    const int textX = margin + (itemWidth - textWidth) / 2;
    const int textY = y + (itemHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    renderer.drawText(UI_10_FONT_ID, textX, textY, name, !selected);
  }

  // Scroll indicators
  if (startIdx > 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY - 20, "^");
  }
  if (endIdx < static_cast<int>(games.size())) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisible * (itemHeight + 5), "v");
  }

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery indicator
  const auto batteryX = screenWidth - 25;
  ScreenComponents::drawBattery(renderer, batteryX, 10, false);

  renderer.displayBuffer();
}
