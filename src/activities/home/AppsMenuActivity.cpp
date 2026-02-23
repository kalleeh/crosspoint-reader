#include "components/UITheme.h"
#include "AppsMenuActivity.h"
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include "../../MappedInputManager.h"

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  render();
}

void AppsMenuActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_APPS_MENU_TITLE));

  // List
  const char* items[] = {tr(STR_ONLINE_MENU_TITLE), tr(STR_GAMES_MENU_TITLE), tr(STR_AWS_MENU_TITLE)};
  constexpr int itemCount = 3;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screenHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, screenWidth, contentHeight},
      itemCount, selectedIndex,
      [&items](int index) { return std::string(items[index]); });

  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AppsMenuActivity::loop() {
  Activity::loop();
  
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      onOnline();
    } else if (selectedIndex == 1) {
      onGames();
    } else {
      onAWSCert();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
             mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex - 1 + 3) % 3;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % 3;
    render();
  }
}
