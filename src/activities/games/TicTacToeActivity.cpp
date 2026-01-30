#include "TicTacToeActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "../../ScreenComponents.h"
#include "../../fontIds.h"

void TicTacToeActivity::onEnter() {
  GameActivity::onEnter();
  resetGame();
  render();
}

void TicTacToeActivity::onExit() { GameActivity::onExit(); }

void TicTacToeActivity::resetGame() {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      board[i][j] = EMPTY;
    }
  }
  cursorX = 1;
  cursorY = 1;
  gameState = PLAYING;
  playerTurn = true;
}

void TicTacToeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  if (gameState != PLAYING) {
    // Game over, wait for confirm to restart
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      resetGame();
      render();
    }
    return;
  }

  bool needsRedraw = false;

  if (playerTurn) {
    // Player's turn
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      cursorX = (cursorX + 2) % 3;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      cursorX = (cursorX + 1) % 3;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      cursorY = (cursorY + 2) % 3;
      needsRedraw = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      cursorY = (cursorY + 1) % 3;
      needsRedraw = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (board[cursorY][cursorX] == EMPTY) {
        makeMove(cursorX, cursorY, PLAYER_X);
        playerTurn = false;
        gameState = checkWinner();
        needsRedraw = true;

        if (gameState == PLAYING) {
          // AI's turn
          delay(200);  // Small delay for better UX
          aiMove();
          gameState = checkWinner();
          playerTurn = true;
          needsRedraw = true;
        }
      }
    }
  }

  if (needsRedraw) {
    render();
  }
}

bool TicTacToeActivity::makeMove(int x, int y, Cell player) {
  if (board[y][x] == EMPTY) {
    board[y][x] = player;
    return true;
  }
  return false;
}

TicTacToeActivity::GameState TicTacToeActivity::checkWinner() {
  // Check rows and columns
  for (int i = 0; i < 3; i++) {
    if (board[i][0] != EMPTY && board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
      return board[i][0] == PLAYER_X ? PLAYER_WIN : AI_WIN;
    }
    if (board[0][i] != EMPTY && board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
      return board[0][i] == PLAYER_X ? PLAYER_WIN : AI_WIN;
    }
  }

  // Check diagonals
  if (board[0][0] != EMPTY && board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
    return board[0][0] == PLAYER_X ? PLAYER_WIN : AI_WIN;
  }
  if (board[0][2] != EMPTY && board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
    return board[0][2] == PLAYER_X ? PLAYER_WIN : AI_WIN;
  }

  // Check for draw
  bool isFull = true;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      if (board[i][j] == EMPTY) {
        isFull = false;
        break;
      }
    }
  }

  return isFull ? DRAW : PLAYING;
}

int TicTacToeActivity::minimax(int depth, bool isMaximizing) {
  GameState state = checkWinner();
  if (state == AI_WIN)
    return 10 - depth;
  if (state == PLAYER_WIN)
    return depth - 10;
  if (state == DRAW)
    return 0;

  if (isMaximizing) {
    int bestScore = -1000;
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (board[i][j] == EMPTY) {
          board[i][j] = PLAYER_O;
          int score = minimax(depth + 1, false);
          board[i][j] = EMPTY;
          bestScore = std::max(bestScore, score);
        }
      }
    }
    return bestScore;
  } else {
    int bestScore = 1000;
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (board[i][j] == EMPTY) {
          board[i][j] = PLAYER_X;
          int score = minimax(depth + 1, true);
          board[i][j] = EMPTY;
          bestScore = std::min(bestScore, score);
        }
      }
    }
    return bestScore;
  }
}

void TicTacToeActivity::aiMove() {
  int bestScore = -1000;
  int bestX = -1;
  int bestY = -1;

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      if (board[i][j] == EMPTY) {
        board[i][j] = PLAYER_O;
        int score = minimax(0, false);
        board[i][j] = EMPTY;

        if (score > bestScore) {
          bestScore = score;
          bestX = j;
          bestY = i;
        }
      }
    }
  }

  if (bestX != -1 && bestY != -1) {
    makeMove(bestX, bestY, PLAYER_O);
  }
}

void TicTacToeActivity::drawCell(int x, int y, Cell cell, bool highlighted) {
  const int cellSize = 80;
  const int gridX = (renderer.getScreenWidth() - 3 * cellSize) / 2;
  const int gridY = 200;
  const int cellX = gridX + x * cellSize;
  const int cellY = gridY + y * cellSize;

  // Draw highlight
  if (highlighted && gameState == PLAYING && playerTurn) {
    renderer.fillRect(cellX + 2, cellY + 2, cellSize - 4, cellSize - 4, false);
  }

  // Draw cell content
  const int margin = 15;
  if (cell == PLAYER_X) {
    // Draw X
    renderer.drawLine(cellX + margin, cellY + margin, cellX + cellSize - margin, cellY + cellSize - margin);
    renderer.drawLine(cellX + cellSize - margin, cellY + margin, cellX + margin, cellY + cellSize - margin);
  } else if (cell == PLAYER_O) {
    // Draw O (as a square for simplicity on e-paper)
    renderer.drawRect(cellX + margin, cellY + margin, cellSize - 2 * margin, cellSize - 2 * margin);
    renderer.drawRect(cellX + margin + 1, cellY + margin + 1, cellSize - 2 * margin - 2, cellSize - 2 * margin - 2);
  }
}

void TicTacToeActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 30, "TIC TAC TOE");

  // Status
  const char* status = "";
  if (gameState == PLAYING) {
    status = playerTurn ? "Your turn (X)" : "AI thinking...";
  } else if (gameState == PLAYER_WIN) {
    status = "You Win!";
  } else if (gameState == AI_WIN) {
    status = "AI Wins!";
  } else if (gameState == DRAW) {
    status = "Draw!";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, 80, status);

  // Draw grid
  const int cellSize = 80;
  const int gridX = (screenWidth - 3 * cellSize) / 2;
  const int gridY = 200;
  drawGrid(gridX, gridY, cellSize, 3, 3, true);

  // Draw cells
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      drawCell(j, i, board[i][j], (j == cursorX && i == cursorY));
    }
  }

  // Instructions
  if (gameState != PLAYING) {
    renderer.drawCenteredText(UI_10_FONT_ID, gridY + 3 * cellSize + 40, "Press Confirm to play again");
  }

  // Button hints
  const char* confirmText = gameState == PLAYING ? "Place" : "Restart";
  const auto labels = mappedInput.mapLabels("Back", confirmText, "Move", "Move");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  ScreenComponents::drawBattery(renderer, screenWidth - 25, 10, false);

  renderer.displayBuffer();
}
