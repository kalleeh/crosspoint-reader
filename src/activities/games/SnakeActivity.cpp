#include "../../DebugConfig.h"
#include "SnakeActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "../../MappedInputManager.h"
#include "../../ScreenComponents.h"
#include "../../fontIds.h"
#include "../../GameConstants.h"

using namespace GameConstants;

void SnakeActivity::onEnter() {
  GameActivity::onEnter();
  resetGame();
  render();
}

void SnakeActivity::onExit() { GameActivity::onExit(); }

void SnakeActivity::resetGame() {
  snake.clear();
  snake.reserve(GRID_SIZE * GRID_SIZE / 4);  // Reserve space for reasonable snake length
  snake.push_back({GRID_SIZE / 2, GRID_SIZE / 2});
  snake.push_back({GRID_SIZE / 2 - 1, GRID_SIZE / 2});
  snake.push_back({GRID_SIZE / 2 - 2, GRID_SIZE / 2});
  direction = RIGHT;
  pendingDirection = RIGHT;
  lastInputDirection = RIGHT;
  gameState = PLAYING;
  score = 0;
  lastMoveTime = millis();
  moveDelay = 200;  // 200ms = 5 FPS for smooth e-ink gameplay
  moveCount = 0;
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

  // Handle input EVERY loop - only update pending if it's a NEW direction
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && direction != DOWN && lastInputDirection != UP) {
    pendingDirection = UP;
    lastInputDirection = UP;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) && direction != UP && lastInputDirection != DOWN) {
    pendingDirection = DOWN;
    lastInputDirection = DOWN;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) && direction != RIGHT && lastInputDirection != LEFT) {
    pendingDirection = LEFT;
    lastInputDirection = LEFT;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) && direction != LEFT && lastInputDirection != RIGHT) {
    pendingDirection = RIGHT;
    lastInputDirection = RIGHT;
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
  moveCount++;

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
    score += SNAKE_SCORE_PER_FOOD;
    spawnFood();
    // Increase speed slightly (but not below 150ms for e-ink)
    if (moveDelay > 150) {
      moveDelay -= SNAKE_SPEED_INCREASE;
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
  // Safety: if the snake fills the entire grid, no valid food position exists
  if (snake.size() >= static_cast<size_t>(GRID_SIZE * GRID_SIZE)) {
    gameOver();
    return;
  }
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

  // Title and score at top
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "SNAKE");

  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
  renderer.drawCenteredText(UI_10_FONT_ID, 40, scoreText);

  // Calculate grid to use maximum vertical space
  const int topMargin = 70;
  const int bottomMargin = 50;  // Space for button hints
  const int availableHeight = screenHeight - topMargin - bottomMargin;
  const int availableWidth = screenWidth - 40;  // 20px margin each side
  
  // Calculate cell size to fit screen
  const int cellSizeByWidth = availableWidth / GRID_SIZE;
  const int cellSizeByHeight = availableHeight / GRID_SIZE;
  const int cellSize = (cellSizeByWidth < cellSizeByHeight) ? cellSizeByWidth : cellSizeByHeight;
  
  const int gridPixelSize = GRID_SIZE * cellSize;
  const int gridX = (screenWidth - gridPixelSize) / 2;
  const int gridY = topMargin;

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

  // Button hints at bottom
  const char* confirmText = gameState == PLAYING ? "" : "Restart";
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Turn", "Turn");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery at top right
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
