#include "customNavigation.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <memory>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
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

// Navigation is driven through the ActivityManager singleton (upstream model).
// onGoHome() is provided by Activity::onGoHome via activityManager.goHome().
static void onGoHome() { activityManager.goHome(); }

// Fork-local state — copies made before the owning activity is replaced/deleted
static String awsCertId;
static String awsMode;
static String awsDomain;

void onGoToApps() {
  activityManager.replaceActivity(
      std::make_unique<AppsMenuActivity>(renderer, mappedInputManager, onGoHome, onGoToOnline, onGoToGames, onGoToAWSCert));
}

void onGoToGames() {
  auto gamesMenu = std::make_unique<GamesMenuActivity>(renderer, mappedInputManager, onGoToApps);

  // Register all games
  gamesMenu->registerGame("tictactoe", "Tic Tac Toe", onGoToTicTacToe);
  gamesMenu->registerGame("snake", "Snake", onGoToSnake);
  gamesMenu->registerGame("2048", "2048", onGoTo2048);
  gamesMenu->registerGame("memory", "Memory Match", onGoToMemoryMatch);
  gamesMenu->registerGame("chess", "Chess", onGoToChess);

  activityManager.replaceActivity(std::move(gamesMenu));
}

void onGoToTicTacToe() {
  activityManager.replaceActivity(std::make_unique<TicTacToeActivity>(renderer, mappedInputManager, onGoToGames));
}

void onGoToSnake() {
  activityManager.replaceActivity(std::make_unique<SnakeActivity>(renderer, mappedInputManager, onGoToGames));
}

void onGoTo2048() {
  activityManager.replaceActivity(std::make_unique<Game2048Activity>(renderer, mappedInputManager, onGoToGames));
}

void onGoToMemoryMatch() {
  activityManager.replaceActivity(std::make_unique<MemoryMatchActivity>(renderer, mappedInputManager, onGoToGames));
}

void onGoToChess() {
  activityManager.replaceActivity(std::make_unique<ChessActivity>(renderer, mappedInputManager, onGoToGames));
}

void onGoToOnline() {
  auto onlineMenu = std::make_unique<OnlineMenuActivity>(renderer, mappedInputManager, onGoToApps);

  // Register online features
  onlineMenu->registerItem("weather", "Weather", onGoToWeather);
  onlineMenu->registerItem("wikipedia", "Wikipedia Feed", onGoToWikipedia);
  onlineMenu->registerItem("word", "Word of the Day", onGoToWordOfDay);
  onlineMenu->registerItem("history", "This Day in History", onGoToHistory);
  onlineMenu->registerItem("xkcd", "XKCD Comics", onGoToXKCD);

  activityManager.replaceActivity(std::move(onlineMenu));
}

void onGoToWeather() {
  activityManager.replaceActivity(std::make_unique<WeatherActivity>(renderer, mappedInputManager, onGoToOnline));
}

void onGoToWikipedia() {
  activityManager.replaceActivity(std::make_unique<WikipediaRandomActivity>(renderer, mappedInputManager, onGoToOnline));
}

void onGoToWordOfDay() {
  activityManager.replaceActivity(std::make_unique<WordOfTheDayActivity>(renderer, mappedInputManager, onGoToOnline));
}

void onGoToHistory() {
  activityManager.replaceActivity(std::make_unique<HistoryTodayActivity>(renderer, mappedInputManager, onGoToOnline));
}

void onGoToXKCD() {
  activityManager.replaceActivity(std::make_unique<XKCDViewerActivity>(renderer, mappedInputManager, onGoToOnline));
}

// AWS Certification Practice
void onGoToAWSCert() {
  activityManager.replaceActivity(
      std::make_unique<AWSCertMenuActivity>(renderer, mappedInputManager, onGoToApps, onGoToAWSPracticeMode));
}

void onGoToAWSPracticeMode(const char* certId) {
  if (certId) awsCertId = certId;

  activityManager.replaceActivity(std::make_unique<AWSPracticeModeActivity>(
      renderer, mappedInputManager, onGoToAWSCert,
      [](const char* mode, const char* domain) { onStartAWSQuiz(awsCertId.c_str(), mode, domain); }, awsCertId.c_str()));
}

void onStartAWSQuiz(const char* certId, const char* mode, const char* domain) {
  // Copy all three strings before the owning activity is replaced/deleted
  if (certId) awsCertId = certId;
  if (mode)   awsMode   = mode;
  if (domain) awsDomain = domain;

  activityManager.replaceActivity(std::make_unique<AWSCertQuizActivity>(
      renderer, mappedInputManager, []() { onGoToAWSPracticeMode(awsCertId.c_str()); }, awsCertId.c_str(),
      awsMode.c_str(), awsDomain.c_str()));
}
