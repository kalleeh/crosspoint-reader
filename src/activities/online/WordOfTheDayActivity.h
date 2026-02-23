#pragma once
#include <functional>
#include "../Activity.h"

class WordOfTheDayActivity final : public Activity {
  enum State { LOADING, LOADED, ERROR };
  
  const std::function<void()> onBack;
  State state = LOADING;
  String word;
  String definition;
  String example;
  int scrollOffset = 0;
  int maxScroll = 0;

  void fetchWord();
  void render();

 public:
  explicit WordOfTheDayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& onBack)
      : Activity("Word of the Day", renderer, mappedInput), onBack(onBack) {}
  
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
