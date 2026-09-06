#pragma once
#include <vector>

#include "../Activity.h"

class HistoryTodayActivity final : public Activity {
  enum State { LOADING, LOADED, ERROR };

  void (*const onBack)();
  State state = LOADING;
  String date;
  std::vector<String> events;
  int scrollOffset = 0;
  int maxScroll = 0;

  void fetchEvents();
  void render();

 public:
  explicit HistoryTodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*onBack)())
      : Activity("This Day in History", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
