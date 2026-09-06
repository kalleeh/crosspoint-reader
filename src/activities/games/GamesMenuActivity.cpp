#include "GamesMenuActivity.h"

#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "../../DebugConfig.h"
#include "../../MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void GamesMenuActivity::registerGame(const std::string& name, const std::string& displayName,
                                     const std::function<void()>& onSelect) {
  games.push_back({name, displayName, onSelect});
}

void GamesMenuActivity::onEnter() {
  rowItems.clear();
  rowItems.reserve(games.size());
  for (const auto& game : games) {
    fui::ListItem item;
    item.label = game.displayName.c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
  UiListActivity::onEnter();
}

void GamesMenuActivity::onExit() {
  Activity::onExit();
  rowItems.clear();  // labels alias games[].displayName
}

const char* GamesMenuActivity::headerTitle() const { return fork_tr(STR_GAMES_MENU_TITLE); }

void GamesMenuActivity::buildScreen(UiScreen& screen) {
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

void GamesMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (games[index].onSelect) {
    app.clearTapFlash();  // leaving this screen
    games[index].onSelect();
  }
}

bool GamesMenuActivity::handleButtons() {
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
