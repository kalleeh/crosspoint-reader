#pragma once

#include <functional>
#include <vector>

#include "GameActivity.h"

class SnakeActivity final : public GameActivity {
 private:
  struct Point {
    int x, y;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
  };

  enum Direction { UP, DOWN, LEFT, RIGHT };
  enum GameState { PLAYING, GAME_OVER };

  static constexpr int GRID_SIZE = 20;
  std::vector<Point> snake;
  Point food;
  Direction direction;
  Direction pendingDirection;
  GameState gameState;
  int score;
  unsigned long lastMoveTime;
  int moveDelay;
  const std::function<void()> onBack;

  void resetGame();
  void render();
  void update();
  void spawnFood();
  bool isCollision(const Point& p);
  void gameOver();

 public:
  explicit SnakeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : GameActivity("Snake", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
};
