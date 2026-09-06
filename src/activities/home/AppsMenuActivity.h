#pragma once
#include <functional>

#include "../UiListActivity.h"

// Fork "Apps" hub: a fixed four-row FreeInkUI list (AWS quiz, Online, Games, Reading stats).
class AppsMenuActivity final : public UiListActivity {
  const std::function<void()> onBack;
  const std::function<void()> onOnline;
  const std::function<void()> onGames;
  const std::function<void()> onAWSCert;
  const std::function<void()> onReadingStats;

  static constexpr int ITEM_COUNT = 4;
  freeink::ui::ListItem rowItems[ITEM_COUNT];

  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack,
                            const std::function<void()>& onOnline, const std::function<void()>& onGames,
                            const std::function<void()>& onAWSCert, const std::function<void()>& onReadingStats)
      : UiListActivity("Apps", renderer, mappedInput),
        onBack(onBack),
        onOnline(onOnline),
        onGames(onGames),
        onAWSCert(onAWSCert),
        onReadingStats(onReadingStats) {}

  void onEnter() override;
};
