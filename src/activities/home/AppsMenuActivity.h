#pragma once
#include <functional>
#include "../Activity.h"

class AppsMenuActivity final : public Activity {
  const std::function<void()> onBack;
  const std::function<void()> onOnline;
  const std::function<void()> onGames;
  const std::function<void()> onAWSCert;
  const std::function<void()> onReadingStats;
  int selectedIndex = 0;

  void render();

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onBack,
                            const std::function<void()>& onOnline,
                            const std::function<void()>& onGames,
                            const std::function<void()>& onAWSCert,
                            const std::function<void()>& onReadingStats)
      : Activity("Apps", renderer, mappedInput),
        onBack(onBack),
        onOnline(onOnline),
        onGames(onGames),
        onAWSCert(onAWSCert),
        onReadingStats(onReadingStats) {}

  void onEnter() override;
  void loop() override;
};
