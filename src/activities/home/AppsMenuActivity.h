#pragma once

#include "../UiListActivity.h"

// Fork "Apps" hub: a fixed four-row FreeInkUI list (AWS quiz, Online, Games, Reading stats).
class AppsMenuActivity final : public UiListActivity {
  void (*const onBack)();
  void (*const onOnline)();
  void (*const onGames)();
  void (*const onAWSCert)();
  void (*const onReadingStats)();

  static constexpr int ITEM_COUNT = 4;
  freeink::ui::ListItem rowItems[ITEM_COUNT];

  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*onBack)(),
                            void (*onOnline)(), void (*onGames)(), void (*onAWSCert)(), void (*onReadingStats)())
      : UiListActivity("Apps", renderer, mappedInput),
        onBack(onBack),
        onOnline(onOnline),
        onGames(onGames),
        onAWSCert(onAWSCert),
        onReadingStats(onReadingStats) {}

  void onEnter() override;
};
