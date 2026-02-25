#include "../../DebugConfig.h"
#include "GamesMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <ForkI18n.h>

#include "../../MappedInputManager.h"

#include "components/UITheme.h"

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  render();
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
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, fork_tr(STR_GAMES_MENU_TITLE));

  // List
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screenHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, screenWidth, contentHeight},
      static_cast<int>(games.size()), selectedIndex,
      [this](int index) { return games[index].displayName; });

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
