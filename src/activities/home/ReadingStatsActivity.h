#pragma once
#include <functional>

#include "../Activity.h"

// Fork: shows reading time / page-turn statistics collected by
// ReadingStatsManager (totals, today, last days).
class ReadingStatsActivity final : public Activity {
  const std::function<void()> onBack;

  void render();

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& onBack)
      : Activity("Reading Stats", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
};
