#include "AppsMenuActivity.h"

#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "../../MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void AppsMenuActivity::onEnter() {
  // Row labels are flash-resident translation strings; the list only holds pointers.
  const char* labels[ITEM_COUNT] = {fork_tr(STR_AWS_MENU_TITLE), fork_tr(STR_ONLINE_MENU_TITLE),
                                    fork_tr(STR_GAMES_MENU_TITLE), fork_tr(STR_RSTATS_TITLE)};
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = labels[i];
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  UiListActivity::onEnter();
}

const char* AppsMenuActivity::headerTitle() const { return fork_tr(STR_APPS_MENU_TITLE); }

void AppsMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void AppsMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= ITEM_COUNT) return;
  app.clearTapFlash();  // leaving this screen
  switch (index) {
    case 0:
      onAWSCert();
      break;
    case 1:
      onOnline();
      break;
    case 2:
      onGames();
      break;
    default:
      onReadingStats();
      break;
  }
}

bool AppsMenuActivity::handleButtons() {
  // Back on press-edge: children also navigate on press, so the release edge
  // of the same physical press must not be re-handled here (it would cascade
  // one extra level up per press).
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}
