#pragma once
#include <functional>
#include "../Activity.h"

class BrowseMenuActivity final : public Activity {
  const std::function<void()> onBack;
  const std::function<void()> onOpdsBrowser;
  const std::function<void()> onFileTransfer;
  int selectedIndex = 0;

  void render();

 public:
  explicit BrowseMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack,
                              const std::function<void()>& onOpdsBrowser,
                              const std::function<void()>& onFileTransfer)
      : Activity("Browse", renderer, mappedInput),
        onBack(onBack),
        onOpdsBrowser(onOpdsBrowser),
        onFileTransfer(onFileTransfer) {}

  void onEnter() override;
  void loop() override;
};
