#include "MemoryMatchActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

void MemoryMatchActivity::onEnter() {
  GameActivity::onEnter();
  resetGame();
  render();
}

void MemoryMatchActivity::onExit() { GameActivity::onExit(); }

void MemoryMatchActivity::resetGame() {
  // Initialize board with pairs
  int values[NUM_PAIRS * 2];
  for (int i = 0; i < NUM_PAIRS; i++) {
    values[i * 2] = i + 1;
    values[i * 2 + 1] = i + 1;
  }

  // Shuffle
  for (int i = 0; i < NUM_PAIRS * 2; i++) {
    int j = random(0, NUM_PAIRS * 2);
    int temp = values[i];
    values[i] = values[j];
    values[j] = temp;
  }

  // Fill board
  int idx = 0;
  for (int i = 0; i < GRID_ROWS; i++) {
    for (int j = 0; j < GRID_COLS; j++) {
      board[i][j].value = values[idx++];
      board[i][j].state = HIDDEN;
    }
  }

  cursorX = 0;
  cursorY = 0;
  hasFirstCard = false;
  gameState = PLAYING;
  moves = 0;
  revealStartTime = 0;
}

void MemoryMatchActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameState == WON) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      render();
    }
    return;
  }

  // Handle auto-hide after 2 seconds
  if (revealStartTime > 0 && millis() - revealStartTime > 2000) {
    checkMatch();
    revealStartTime = 0;
    render();
  }

  bool needsRedraw = false;

  if (revealStartTime == 0) {  // Only allow input when not showing mismatched pair
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      cursorX = (cursorX + GRID_COLS - 1) % GRID_COLS;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      cursorX = (cursorX + 1) % GRID_COLS;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      cursorY = (cursorY + GRID_ROWS - 1) % GRID_ROWS;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      cursorY = (cursorY + 1) % GRID_ROWS;
      needsRedraw = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      Card& card = board[cursorY][cursorX];

      if (card.state == HIDDEN) {
        card.state = REVEALED;

        if (!hasFirstCard) {
          firstCardX = cursorX;
          firstCardY = cursorY;
          hasFirstCard = true;
        } else {
          moves++;
          revealStartTime = millis();
        }
        needsRedraw = true;
      }
    }
  }

  if (needsRedraw) {
    render();
  }
}

void MemoryMatchActivity::checkMatch() {
  Card& first = board[firstCardY][firstCardX];
  Card& second = board[cursorY][cursorX];

  if (first.value == second.value) {
    // Match!
    first.state = MATCHED;
    second.state = MATCHED;

    // Check if all matched
    bool allMatched = true;
    for (int i = 0; i < GRID_ROWS; i++) {
      for (int j = 0; j < GRID_COLS; j++) {
        if (board[i][j].state != MATCHED) {
          allMatched = false;
          break;
        }
      }
    }

    if (allMatched) {
      gameState = WON;
    }
  } else {
    // No match - hide both
    first.state = HIDDEN;
    second.state = HIDDEN;
  }

  hasFirstCard = false;
}

void MemoryMatchActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 20, "MEMORY MATCH");

  // Moves counter
  char movesText[32];
  snprintf(movesText, sizeof(movesText), "Moves: %d", moves);
  renderer.drawCenteredText(UI_10_FONT_ID, 50, movesText);

  // Draw grid
  const int cellSize = 70;
  const int margin = 8;
  const int gridWidth = GRID_COLS * cellSize + (GRID_COLS - 1) * margin;
  const int gridHeight = GRID_ROWS * cellSize + (GRID_ROWS - 1) * margin;
  const int gridX = (screenWidth - gridWidth) / 2;
  const int gridY = (screenHeight - gridHeight) / 2 + 20;

  for (int i = 0; i < GRID_ROWS; i++) {
    for (int j = 0; j < GRID_COLS; j++) {
      const Card& card = board[i][j];
      const int x = gridX + j * (cellSize + margin);
      const int y = gridY + i * (cellSize + margin);
      const bool selected = (j == cursorX && i == cursorY);

      // Draw card
      if (card.state == MATCHED) {
        // Matched - show as empty
        renderer.drawRect(x, y, cellSize, cellSize);
      } else if (card.state == REVEALED) {
        // Revealed - show value
        renderer.fillRect(x, y, cellSize, cellSize);
        renderer.drawRect(x, y, cellSize, cellSize, false);

        char valueText[4];
        snprintf(valueText, sizeof(valueText), "%d", card.value);
        const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, valueText);
        const int textX = x + (cellSize - textWidth) / 2;
        const int textY = y + (cellSize - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
        renderer.drawText(UI_12_FONT_ID, textX, textY, valueText, false);
      } else {
        // Hidden - show as filled rectangle
        renderer.fillRect(x, y, cellSize, cellSize);
      }

      // Draw selection highlight
      if (selected && card.state == HIDDEN && revealStartTime == 0) {
        renderer.drawRect(x + 2, y + 2, cellSize - 4, cellSize - 4, false);
      }
    }
  }

  // Win message
  if (gameState == WON) {
    const int boxWidth = 300;
    const int boxHeight = 120;
    const int boxX = (screenWidth - boxWidth) / 2;
    const int boxY = (screenHeight - boxHeight) / 2;

    renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight);
    renderer.drawRect(boxX + 1, boxY + 1, boxWidth - 2, boxHeight - 2);

    renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, "YOU WIN!");

    char finalMoves[32];
    snprintf(finalMoves, sizeof(finalMoves), "Moves: %d", moves);
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 60, finalMoves);

    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 90, "Press Confirm to play again");
  }

  // Button hints
  const char* confirmText = gameState == PLAYING ? "Reveal" : "Restart";
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Move", "Move");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer();
}
