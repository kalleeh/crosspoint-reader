#include "OnlineMenuActivity.h"

#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "../../MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void OnlineMenuActivity::registerItem(const std::string& displayName, void (*onSelect)()) {
  menuItems.push_back({displayName, onSelect});
}

void OnlineMenuActivity::onEnter() {
  rowItems.clear();
  rowItems.reserve(menuItems.size());
  for (const auto& entry : menuItems) {
    fui::ListItem item;
    item.label = entry.displayName.c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
  UiListActivity::onEnter();
}

void OnlineMenuActivity::onExit() {
  Activity::onExit();
  rowItems.clear();  // labels alias menuItems[].displayName
}

const char* OnlineMenuActivity::headerTitle() const { return fork_tr(STR_ONLINE_MENU_TITLE); }

void OnlineMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void OnlineMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (menuItems[index].onSelect) {
    app.clearTapFlash();  // leaving this screen
    menuItems[index].onSelect();
  }
}

bool OnlineMenuActivity::handleButtons() {
  // Back on press-edge, consistent across all fork activities: mixing press
  // and release edges makes one physical press navigate two levels up.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack) onBack();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}
