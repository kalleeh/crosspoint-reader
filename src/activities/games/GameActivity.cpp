#include "GameActivity.h"

#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <SDCardManager.h>

#include "../../DebugConfig.h"
#include "../../fontIds.h"

std::string GameActivity::getGameDataDir() const { return gameDataPath; }

void GameActivity::drawCenteredMessage(const char* message, int y) {
  renderer.drawCenteredText(UI_12_FONT_ID, y, message);
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
