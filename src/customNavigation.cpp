#include "customNavigation.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "activities/home/AppsMenuActivity.h"
#include "activities/games/ChessActivity.h"
#include "activities/games/Game2048Activity.h"
#include "activities/games/GamesMenuActivity.h"
#include "activities/games/MemoryMatchActivity.h"
#include "activities/games/SnakeActivity.h"
#include "activities/games/TicTacToeActivity.h"
#include "activities/learning/AWSCertMenuActivity.h"
#include "activities/learning/AWSCertQuizActivity.h"
#include "activities/learning/AWSPracticeModeActivity.h"
#include "activities/online/HistoryTodayActivity.h"
#include "activities/online/OnlineMenuActivity.h"
#include "activities/online/WeatherActivity.h"
#include "activities/online/WikipediaRandomActivity.h"
#include "activities/online/WordOfTheDayActivity.h"
#include "activities/online/XKCDViewerActivity.h"

// Globals defined in main.cpp
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

// Free functions defined in main.cpp
void exitActivity();
void enterNewActivity(Activity* activity);

// Forward declarations for callbacks used by the browse/apps submenu
// (these are also in main.cpp but used here as callbacks)
void onGoHome();
void onGoToBrowser();
void onGoToFileTransfer();

// Fork-local state
static String awsCertId;

void onGoToApps() {
  exitActivity();
  enterNewActivity(new AppsMenuActivity(renderer, mappedInputManager, onGoHome, onGoToOnline, onGoToGames, onGoToAWSCert));
}

void onGoToGames() {
  exitActivity();
  auto* gamesMenu = new GamesMenuActivity(renderer, mappedInputManager, onGoToApps);

  // Register all games
  gamesMenu->registerGame("tictactoe", "Tic Tac Toe", onGoToTicTacToe);
  gamesMenu->registerGame("snake", "Snake", onGoToSnake);
  gamesMenu->registerGame("2048", "2048", onGoTo2048);
  gamesMenu->registerGame("memory", "Memory Match", onGoToMemoryMatch);
  gamesMenu->registerGame("chess", "Chess", onGoToChess);

  enterNewActivity(gamesMenu);
}

void onGoToTicTacToe() {
  exitActivity();
  enterNewActivity(new TicTacToeActivity(renderer, mappedInputManager, onGoToGames));
}

void onGoToSnake() {
  exitActivity();
  enterNewActivity(new SnakeActivity(renderer, mappedInputManager, onGoToGames));
}

void onGoTo2048() {
  exitActivity();
  enterNewActivity(new Game2048Activity(renderer, mappedInputManager, onGoToGames));
}

void onGoToMemoryMatch() {
  exitActivity();
  enterNewActivity(new MemoryMatchActivity(renderer, mappedInputManager, onGoToGames));
}

void onGoToChess() {
  exitActivity();
  enterNewActivity(new ChessActivity(renderer, mappedInputManager, onGoToGames));
}

void onGoToOnline() {
  exitActivity();
  auto* onlineMenu = new OnlineMenuActivity(renderer, mappedInputManager, onGoToApps);

  // Register online features
  onlineMenu->registerItem("weather", "Weather", onGoToWeather);
  onlineMenu->registerItem("wikipedia", "Wikipedia Feed", onGoToWikipedia);
  onlineMenu->registerItem("word", "Word of the Day", onGoToWordOfDay);
  onlineMenu->registerItem("history", "This Day in History", onGoToHistory);
  onlineMenu->registerItem("xkcd", "XKCD Comics", onGoToXKCD);

  enterNewActivity(onlineMenu);
}

void onGoToWeather() {
  exitActivity();
  enterNewActivity(new WeatherActivity(renderer, mappedInputManager, onGoToOnline));
}

void onGoToWikipedia() {
  exitActivity();
  enterNewActivity(new WikipediaRandomActivity(renderer, mappedInputManager, onGoToOnline));
}

void onGoToWordOfDay() {
  exitActivity();
  enterNewActivity(new WordOfTheDayActivity(renderer, mappedInputManager, onGoToOnline));
}

void onGoToHistory() {
  exitActivity();
  enterNewActivity(new HistoryTodayActivity(renderer, mappedInputManager, onGoToOnline));
}

void onGoToXKCD() {
  exitActivity();
  enterNewActivity(new XKCDViewerActivity(renderer, mappedInputManager, onGoToOnline));
}

// AWS Certification Practice
void onGoToAWSCert() {
  exitActivity();
  enterNewActivity(new AWSCertMenuActivity(renderer, mappedInputManager, onGoToApps, onGoToAWSPracticeMode));
}

void onGoToAWSPracticeMode(const char* certId) {
  if (certId) awsCertId = certId;

  exitActivity();
  enterNewActivity(new AWSPracticeModeActivity(renderer, mappedInputManager, onGoToAWSCert,
    [](const char* mode, const char* domain) {
      onStartAWSQuiz(awsCertId.c_str(), mode, domain);
    }, awsCertId.c_str()));
}

void onStartAWSQuiz(const char* certId, const char* mode, const char* domain) {
  if (certId) awsCertId = certId;

  exitActivity();
  enterNewActivity(new AWSCertQuizActivity(renderer, mappedInputManager,
    []() { onGoToAWSPracticeMode(awsCertId.c_str()); },
    certId, mode, domain));
}
