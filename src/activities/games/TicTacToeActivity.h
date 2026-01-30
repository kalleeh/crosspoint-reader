#pragma once

#include <functional>

#include "GameActivity.h"

class TicTacToeActivity final : public GameActivity {
 private:
  enum Cell { EMPTY = 0, PLAYER_X = 1, PLAYER_O = 2 };
  enum GameState { PLAYING, PLAYER_WIN, AI_WIN, DRAW };

  Cell board[3][3];
  int cursorX, cursorY;
  GameState gameState;
  bool playerTurn;
  const std::function<void()> onBack;

  void resetGame();
  void render();
  bool makeMove(int x, int y, Cell player);
  void aiMove();
  GameState checkWinner();
  int minimax(int depth, bool isMaximizing);
  void drawCell(int x, int y, Cell cell, bool highlighted);

 public:
  explicit TicTacToeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onBack)
      : GameActivity("TicTacToe", renderer, mappedInput), cursorX(1), cursorY(1), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
