#pragma once

#include <functional>

#include "GameActivity.h"

class Game2048Activity final : public GameActivity {
 private:
  enum GameState { PLAYING, GAME_OVER, WON };

  static constexpr int GRID_SIZE = 4;
  int board[GRID_SIZE][GRID_SIZE];
  GameState gameState;
  int score;
  bool hasWon;
  const std::function<void()> onBack;

  void resetGame();
  void render();
  void addRandomTile();
  bool move(int dx, int dy);
  bool canMove();
  void drawTile(int x, int y, int value);

 public:
  explicit Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : GameActivity("2048", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
