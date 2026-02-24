#include "../../DebugConfig.h"
#include "MemoryMatchActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "../../MappedInputManager.h"

#include "components/UITheme.h"
#include "../../fontIds.h"
#include <I18n.h>

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
  renderer.drawCenteredText(UI_12_FONT_ID, 20, tr(STR_GAME_MEMORY_TITLE));

  // Moves counter
  char movesText[32];
  snprintf(movesText, sizeof(movesText), "%s %d", tr(STR_GAME_MOVES), moves);
  renderer.drawCenteredText(UI_10_FONT_ID, 50, movesText);

  // Calculate optimal card size
  const int topMargin = 100;  // More space for title and moves
  const int bottomMargin = 50;
  const int availableHeight = screenHeight - topMargin - bottomMargin;
  const int availableWidth = screenWidth - 40;
  const int margin = 10;
  
  const int cellSizeByWidth = (availableWidth - (GRID_COLS - 1) * margin) / GRID_COLS;
  const int cellSizeByHeight = (availableHeight - (GRID_ROWS - 1) * margin) / GRID_ROWS;
  const int cellSize = (cellSizeByWidth < cellSizeByHeight) ? cellSizeByWidth : cellSizeByHeight;
  
  const int gridWidth = GRID_COLS * cellSize + (GRID_COLS - 1) * margin;
  const int gridHeight = GRID_ROWS * cellSize + (GRID_ROWS - 1) * margin;
  const int gridX = (screenWidth - gridWidth) / 2;
  const int gridY = topMargin;

  // Card symbols - use simple ASCII letters
  const char* symbols[] = {"A", "B", "C", "D", "E", "F", "G", "H"};

  for (int i = 0; i < GRID_ROWS; i++) {
    for (int j = 0; j < GRID_COLS; j++) {
      const Card& card = board[i][j];
      const int x = gridX + j * (cellSize + margin);
      const int y = gridY + i * (cellSize + margin);
      const bool selected = (j == cursorX && i == cursorY);

      // Draw card
      if (card.state == MATCHED) {
        // Matched - show as empty with light border
        renderer.drawRect(x, y, cellSize, cellSize);
        renderer.drawRect(x + 1, y + 1, cellSize - 2, cellSize - 2);
        
        // Draw selection highlight on matched cards (inverted - black on white)
        if (selected) {
          renderer.fillRect(x + 3, y + 3, cellSize - 6, cellSize - 6);
          renderer.fillRect(x + 5, y + 5, cellSize - 10, cellSize - 10);
        }
      } else if (card.state == REVEALED) {
        // Revealed - show letter with filled background
        renderer.fillRect(x, y, cellSize, cellSize);
        renderer.drawRect(x, y, cellSize, cellSize, false);

        // Draw letter in white (inverted)
        const char* symbol = symbols[card.value % 8];
        const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, symbol);
        const int textX = x + (cellSize - textWidth) / 2;
        const int textY = y + (cellSize - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
        renderer.drawText(UI_12_FONT_ID, textX, textY, symbol, false);
        
        // Add decorative pattern based on value
        const int centerX = x + cellSize / 2;
        const int centerY = y + cellSize / 2 + 15;
        const int shapeSize = cellSize / 5;
        
        switch (card.value % 4) {
          case 0: // Small square
            renderer.fillRect(centerX - shapeSize, centerY - shapeSize, shapeSize * 2, shapeSize * 2, false);
            break;
          case 1: // Horizontal lines
            renderer.drawLine(x + 10, centerY - 5, x + cellSize - 10, centerY - 5, false);
            renderer.drawLine(x + 10, centerY, x + cellSize - 10, centerY, false);
            renderer.drawLine(x + 10, centerY + 5, x + cellSize - 10, centerY + 5, false);
            break;
          case 2: // Vertical lines
            renderer.drawLine(centerX - 5, y + 10, centerX - 5, y + cellSize - 10, false);
            renderer.drawLine(centerX, y + 10, centerX, y + cellSize - 10, false);
            renderer.drawLine(centerX + 5, y + 10, centerX + 5, y + cellSize - 10, false);
            break;
          case 3: // Cross
            renderer.drawLine(centerX - shapeSize, centerY - shapeSize, centerX + shapeSize, centerY + shapeSize, false);
            renderer.drawLine(centerX + shapeSize, centerY - shapeSize, centerX - shapeSize, centerY + shapeSize, false);
            break;
        }
      } else {
        // Hidden - show as filled card with pattern
        renderer.fillRect(x, y, cellSize, cellSize);
        renderer.drawRect(x, y, cellSize, cellSize, false);
        // Add diagonal lines for texture
        renderer.drawLine(x + 5, y + 5, x + cellSize - 5, y + cellSize - 5, false);
        renderer.drawLine(x + cellSize - 5, y + 5, x + 5, y + cellSize - 5, false);
      }

      // Draw selection highlight
      if (selected && card.state == HIDDEN && revealStartTime == 0) {
        renderer.drawRect(x + 3, y + 3, cellSize - 6, cellSize - 6, false);
        renderer.drawRect(x + 5, y + 5, cellSize - 10, cellSize - 10, false);
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

    renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, tr(STR_GAME_YOU_WIN));

    char finalMoves[32];
    snprintf(finalMoves, sizeof(finalMoves), "%s %d", tr(STR_GAME_MOVES), moves);
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 60, finalMoves);

    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 90, tr(STR_GAME_PRESS_CONFIRM_AGAIN));
  }

  // Button hints
  const char* confirmText = gameState == PLAYING ? tr(STR_GAME_REVEAL) : tr(STR_GAME_RESTART);
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Move", "Move");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawBatteryRight(renderer, Rect{screenWidth - 25, 10, metrics.batteryWidth, metrics.batteryHeight}, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
