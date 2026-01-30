#include "GameActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "../../fontIds.h"

std::string GameActivity::getGameDataDir() const { return gameDataPath; }

void GameActivity::drawCenteredMessage(const char* message, int y) {
  renderer.drawCenteredText(UI_12_FONT_ID, y, message);
}

void GameActivity::drawPauseMenu() {
  const int boxWidth = 300;
  const int boxHeight = 150;
  const int boxX = (renderer.getScreenWidth() - boxWidth) / 2;
  const int boxY = (renderer.getScreenHeight() - boxHeight) / 2;

  // Draw dialog box
  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, false);
  renderer.drawRect(boxX, boxY, boxWidth, boxHeight);
  renderer.drawRect(boxX + 1, boxY + 1, boxWidth - 2, boxHeight - 2);

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, boxY + 30, "PAUSED");

  // Instructions
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + 70, "Press Back to resume");
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + 95, "Hold Back to quit");
}

void GameActivity::drawGrid(int gridX, int gridY, int cellSize, int rows, int cols, bool thick) {
  // Draw grid lines
  for (int i = 0; i <= rows; i++) {
    int y = gridY + i * cellSize;
    renderer.drawLine(gridX, y, gridX + cols * cellSize, y);
    if (thick && (i == 0 || i == rows)) {
      renderer.drawLine(gridX, y + 1, gridX + cols * cellSize, y + 1);
    }
  }
  for (int i = 0; i <= cols; i++) {
    int x = gridX + i * cellSize;
    renderer.drawLine(x, gridY, x, gridY + rows * cellSize);
    if (thick && (i == 0 || i == cols)) {
      renderer.drawLine(x + 1, gridY, x + 1, gridY + rows * cellSize);
    }
  }
}

void GameActivity::drawButton(int x, int y, int width, int height, const char* text, bool selected) {
  if (selected) {
    renderer.fillRect(x, y, width, height);
    renderer.drawText(UI_10_FONT_ID, x + (width - renderer.getTextWidth(UI_10_FONT_ID, text)) / 2,
                      y + (height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, text, false);
  } else {
    renderer.drawRect(x, y, width, height);
    renderer.drawText(UI_10_FONT_ID, x + (width - renderer.getTextWidth(UI_10_FONT_ID, text)) / 2,
                      y + (height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, text, true);
  }
}
