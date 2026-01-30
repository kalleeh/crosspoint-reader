#include "SnakeActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

void SnakeActivity::onEnter() {
  GameActivity::onEnter();
  resetGame();
  render();
}

void SnakeActivity::onExit() { GameActivity::onExit(); }

void SnakeActivity::resetGame() {
  snake.clear();
  snake.push_back({GRID_SIZE / 2, GRID_SIZE / 2});
  snake.push_back({GRID_SIZE / 2 - 1, GRID_SIZE / 2});
  snake.push_back({GRID_SIZE / 2 - 2, GRID_SIZE / 2});
  direction = RIGHT;
  pendingDirection = RIGHT;
  gameState = PLAYING;
  score = 0;
  lastMoveTime = millis();
  moveDelay = 300;  // ms between moves
  spawnFood();
}

void SnakeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameState == GAME_OVER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      render();
    }
    return;
  }

  // Handle input (queue next direction to prevent double-turns)
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && direction != DOWN) {
    pendingDirection = UP;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) && direction != UP) {
    pendingDirection = DOWN;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) && direction != RIGHT) {
    pendingDirection = LEFT;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) && direction != LEFT) {
    pendingDirection = RIGHT;
  }

  // Update game at fixed interval
  if (millis() - lastMoveTime >= static_cast<unsigned long>(moveDelay)) {
    lastMoveTime = millis();
    update();
    render();
  }
}

void SnakeActivity::update() {
  if (gameState != PLAYING)
    return;

  direction = pendingDirection;

  // Calculate new head position
  Point newHead = snake[0];
  switch (direction) {
    case UP:
      newHead.y--;
      break;
    case DOWN:
      newHead.y++;
      break;
    case LEFT:
      newHead.x--;
      break;
    case RIGHT:
      newHead.x++;
      break;
  }

  // Check collisions
  if (isCollision(newHead)) {
    gameOver();
    return;
  }

  // Move snake
  snake.insert(snake.begin(), newHead);

  // Check if food eaten
  if (newHead == food) {
    score += 10;
    spawnFood();
    // Increase speed slightly
    if (moveDelay > 100) {
      moveDelay -= 10;
    }
  } else {
    snake.pop_back();
  }
}

bool SnakeActivity::isCollision(const Point& p) {
  // Wall collision
  if (p.x < 0 || p.x >= GRID_SIZE || p.y < 0 || p.y >= GRID_SIZE) {
    return true;
  }

  // Self collision
  for (const auto& segment : snake) {
    if (segment == p) {
      return true;
    }
  }

  return false;
}

void SnakeActivity::spawnFood() {
  do {
    food.x = random(0, GRID_SIZE);
    food.y = random(0, GRID_SIZE);
  } while (isCollision(food));
}

void SnakeActivity::gameOver() { gameState = GAME_OVER; }

void SnakeActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Title and score
  renderer.drawCenteredText(UI_12_FONT_ID, 20, "SNAKE");

  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
  renderer.drawCenteredText(UI_10_FONT_ID, 50, scoreText);

  // Calculate grid position and cell size
  const int cellSize = 18;
  const int gridPixelSize = GRID_SIZE * cellSize;
  const int gridX = (screenWidth - gridPixelSize) / 2;
  const int gridY = (screenHeight - gridPixelSize) / 2 + 20;

  // Draw grid border
  renderer.drawRect(gridX - 1, gridY - 1, gridPixelSize + 2, gridPixelSize + 2);

  // Draw snake
  for (size_t i = 0; i < snake.size(); i++) {
    const auto& segment = snake[i];
    const int x = gridX + segment.x * cellSize;
    const int y = gridY + segment.y * cellSize;

    if (i == 0) {
      // Head - draw as filled square with border
      renderer.fillRect(x + 1, y + 1, cellSize - 2, cellSize - 2);
    } else {
      // Body - draw as filled square
      renderer.fillRect(x + 2, y + 2, cellSize - 4, cellSize - 4);
    }
  }

  // Draw food
  const int foodX = gridX + food.x * cellSize;
  const int foodY = gridY + food.y * cellSize;
  renderer.drawRect(foodX + 2, foodY + 2, cellSize - 4, cellSize - 4);
  renderer.drawRect(foodX + 4, foodY + 4, cellSize - 8, cellSize - 8);

  // Game over message
  if (gameState == GAME_OVER) {
    const int boxWidth = 280;
    const int boxHeight = 120;
    const int boxX = (screenWidth - boxWidth) / 2;
    const int boxY = (screenHeight - boxHeight) / 2;

    renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight);
    renderer.drawRect(boxX + 1, boxY + 1, boxWidth - 2, boxHeight - 2);

    renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, "GAME OVER");

    char finalScore[32];
    snprintf(finalScore, sizeof(finalScore), "Final Score: %d", score);
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 60, finalScore);

    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 90, "Press Confirm to restart");
  }

  // Button hints
  const char* confirmText = gameState == PLAYING ? "" : "Restart";
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Turn", "Turn");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer();
}
