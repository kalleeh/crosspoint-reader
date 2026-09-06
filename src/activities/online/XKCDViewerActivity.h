#pragma once
#include <functional>

#include "../Activity.h"

class XKCDViewerActivity final : public Activity {
  enum State { LOADING, LOADED, ERROR };

  const std::function<void()> onBack;
  State state = LOADING;
  int currentComic = 0;
  int maxComic = 0;
  String title;
  String alt;
  String imageUrl;
  int scrollOffset = 0;
  int maxScroll = 0;
  bool imageLoaded = false;

  void fetchComic(int num = 0);
  void downloadAndDisplayImage();
  void drawCachedComicImage();
  void applyOrientationForImage(const char* pngPath);
  void render();

 public:
  explicit XKCDViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack)
      : Activity("XKCD Comics", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
