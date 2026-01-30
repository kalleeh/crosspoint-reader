#include "Game2048Activity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

void Game2048Activity::onEnter() {
  GameActivity::onEnter();
  resetGame();
  render();
}

void Game2048Activity::onExit() { GameActivity::onExit(); }

void Game2048Activity::resetGame() {
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      board[i][j] = 0;
    }
  }
  score = 0;
  gameState = PLAYING;
  hasWon = false;
  addRandomTile();
  addRandomTile();
}

void Game2048Activity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameState != PLAYING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      render();
    }
    return;
  }

  bool moved = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moved = move(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moved = move(0, 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moved = move(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moved = move(1, 0);
  }

  if (moved) {
    addRandomTile();

    // Check for 2048 tile (win condition)
    if (!hasWon) {
      for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
          if (board[i][j] == 2048) {
            hasWon = true;
            gameState = WON;
          }
        }
      }
    }

    // Check for game over
    if (!canMove() && gameState != WON) {
      gameState = GAME_OVER;
    }

    render();
  }
}

void Game2048Activity::addRandomTile() {
  // Find empty cells
  int emptyCells[GRID_SIZE * GRID_SIZE][2];
  int emptyCount = 0;

  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      if (board[i][j] == 0) {
        emptyCells[emptyCount][0] = i;
        emptyCells[emptyCount][1] = j;
        emptyCount++;
      }
    }
  }

  if (emptyCount > 0) {
    int idx = random(0, emptyCount);
    int value = random(0, 10) < 9 ? 2 : 4;  // 90% chance of 2, 10% chance of 4
    board[emptyCells[idx][0]][emptyCells[idx][1]] = value;
  }
}

bool Game2048Activity::move(int dx, int dy) {
  bool moved = false;
  bool merged[GRID_SIZE][GRID_SIZE] = {false};

  int startX = (dx > 0) ? GRID_SIZE - 2 : 1;
  int endX = (dx > 0) ? -1 : GRID_SIZE;
  int stepX = (dx > 0) ? -1 : 1;

  int startY = (dy > 0) ? GRID_SIZE - 2 : 1;
  int endY = (dy > 0) ? -1 : GRID_SIZE;
  int stepY = (dy > 0) ? -1 : 1;

  for (int i = startY; i != endY; i += stepY) {
    for (int j = startX; j != endX; j += stepX) {
      if (board[i][j] == 0)
        continue;

      int newY = i;
      int newX = j;

      // Move as far as possible
      while (true) {
        int nextY = newY + dy;
        int nextX = newX + dx;

        if (nextY < 0 || nextY >= GRID_SIZE || nextX < 0 || nextX >= GRID_SIZE)
          break;

        if (board[nextY][nextX] == 0) {
          newY = nextY;
          newX = nextX;
        } else if (board[nextY][nextX] == board[i][j] && !merged[nextY][nextX]) {
          newY = nextY;
          newX = nextX;
          break;
        } else {
          break;
        }
      }

      if (newY != i || newX != j) {
        if (board[newY][newX] == board[i][j]) {
          // Merge
          board[newY][newX] *= 2;
          score += board[newY][newX];
          merged[newY][newX] = true;
        } else {
          board[newY][newX] = board[i][j];
        }
        board[i][j] = 0;
        moved = true;
      }
    }
  }

  return moved;
}

bool Game2048Activity::canMove() {
  // Check for empty cells
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      if (board[i][j] == 0)
        return true;
    }
  }

  // Check for possible merges
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      if (j < GRID_SIZE - 1 && board[i][j] == board[i][j + 1])
        return true;
      if (i < GRID_SIZE - 1 && board[i][j] == board[i + 1][j])
        return true;
    }
  }

  return false;
}

void Game2048Activity::drawTile(int x, int y, int value) {
  const int cellSize = 90;
  const int margin = 5;
  const int screenWidth = renderer.getScreenWidth();
  const int gridPixelSize = GRID_SIZE * cellSize + (GRID_SIZE + 1) * margin;
  const int gridX = (screenWidth - gridPixelSize) / 2;
  const int gridY = 150;

  const int tileX = gridX + x * (cellSize + margin) + margin;
  const int tileY = gridY + y * (cellSize + margin) + margin;

  if (value == 0) {
    // Empty cell - just draw border
    renderer.drawRect(tileX, tileY, cellSize, cellSize);
    return;
  }

  // Filled cell
  renderer.fillRect(tileX, tileY, cellSize, cellSize);
  renderer.drawRect(tileX, tileY, cellSize, cellSize, false);

  // Draw value
  char text[8];
  snprintf(text, sizeof(text), "%d", value);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, text);
  const int textX = tileX + (cellSize - textWidth) / 2;
  const int textY = tileY + (cellSize - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, text, false);
}

void Game2048Activity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Title and score
  renderer.drawCenteredText(UI_12_FONT_ID, 20, "2048");

  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), "Score: %d", score);
  renderer.drawCenteredText(UI_10_FONT_ID, 50, scoreText);

  // Instructions
  renderer.drawCenteredText(UI_10_FONT_ID, 80, "Combine tiles to reach 2048!");

  // Draw tiles
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      drawTile(j, i, board[i][j]);
    }
  }

  // Game over/win message
  if (gameState != PLAYING) {
    const int boxWidth = 300;
    const int boxHeight = 140;
    const int boxX = (screenWidth - boxWidth) / 2;
    const int boxY = (screenHeight - boxHeight) / 2;

    renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight);
    renderer.drawRect(boxX + 1, boxY + 1, boxWidth - 2, boxHeight - 2);

    if (gameState == WON) {
      renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, "YOU WIN!");
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 60, "You reached 2048!");
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, "GAME OVER");
      char finalScore[32];
      snprintf(finalScore, sizeof(finalScore), "Final Score: %d", score);
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 60, finalScore);
    }

    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 100, "Press Confirm to restart");
  }

  // Button hints
  const char* confirmText = gameState == PLAYING ? "" : "Restart";
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Slide", "Slide");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer();
}
