#pragma once
#include <functional>
#include "../Activity.h"

class AppsMenuActivity final : public Activity {
  const std::function<void()> onBack;
  const std::function<void()> onOnline;
  const std::function<void()> onGames;
  const std::function<void()> onAWSCert;
  int selectedIndex = 0;

  void render();

 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onBack,
                            const std::function<void()>& onOnline,
                            const std::function<void()>& onGames,
                            const std::function<void()>& onAWSCert)
      : Activity("Apps", renderer, mappedInput),
        onBack(onBack),
        onOnline(onOnline),
        onGames(onGames),
        onAWSCert(onAWSCert) {}

  void onEnter() override;
  void loop() override;
};
