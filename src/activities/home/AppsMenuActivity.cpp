#include "components/UITheme.h"
#include "AppsMenuActivity.h"
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <ForkI18n.h>
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
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, fork_tr(STR_APPS_MENU_TITLE));

  // List
  const char* items[] = {fork_tr(STR_AWS_MENU_TITLE), fork_tr(STR_ONLINE_MENU_TITLE), fork_tr(STR_GAMES_MENU_TITLE),
                         fork_tr(STR_RSTATS_TITLE)};
  constexpr int itemCount = 4;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screenHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, screenWidth, contentHeight},
      itemCount, selectedIndex,
      [&items](int index) { return std::string(items[index]); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AppsMenuActivity::loop() {
  Activity::loop();
  
  // Back on press-edge: children also navigate on press, so the release edge
  // of the same physical press must not be re-handled here (it would cascade
  // one extra level up per press).
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      onAWSCert();
    } else if (selectedIndex == 1) {
      onOnline();
    } else if (selectedIndex == 2) {
      onGames();
    } else {
      onReadingStats();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
             mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex - 1 + 4) % 4;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % 4;
    render();
  }
}
