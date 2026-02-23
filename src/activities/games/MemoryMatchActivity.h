#pragma once

#include <functional>

#include "GameActivity.h"

class MemoryMatchActivity final : public GameActivity {
 private:
  enum GameState { PLAYING, WON };
  enum CardState { HIDDEN, REVEALED, MATCHED };

  static constexpr int GRID_ROWS = 4;
  static constexpr int GRID_COLS = 4;
  static constexpr int NUM_PAIRS = (GRID_ROWS * GRID_COLS) / 2;

  struct Card {
    int value;
    CardState state;
  };

  Card board[GRID_ROWS][GRID_COLS];
  int cursorX, cursorY;
  int firstCardX, firstCardY;
  bool hasFirstCard;
  GameState gameState;
  int moves;
  unsigned long revealStartTime;
  const std::function<void()> onBack;

  void resetGame();
  void render();
  void revealCard(int x, int y);
  void checkMatch();
  void shuffleBoard();

 public:
  explicit MemoryMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::function<void()>& onBack)
      : GameActivity("MemoryMatch", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
