#pragma once

#include <string>

#include "../Activity.h"

// Base class for all game activities providing common functionality
class GameActivity : public Activity {
 protected:
  std::string gameName;
  std::string gameDataPath;
  bool isPaused;

  // Helper to get game data directory path
  std::string getGameDataDir() const;

  // Save/load helpers (to be implemented by derived classes if needed)
  virtual bool saveGameState() { return true; }
  virtual bool loadGameState() { return true; }

  // Common UI helpers
  void drawCenteredMessage(const char* message, int y);
  void drawPauseMenu();
  void drawGrid(int gridX, int gridY, int cellSize, int rows, int cols, bool thick = false);
  void drawButton(int x, int y, int width, int height, const char* text, bool selected);

 public:
  explicit GameActivity(const std::string& gameName, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(gameName, renderer, mappedInput), gameName(gameName), isPaused(false) {
    gameDataPath = "/.crosspoint/games/" + gameName;
  }

  virtual ~GameActivity() = default;
};
