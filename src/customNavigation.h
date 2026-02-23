#pragma once
// Forward-declares all fork-specific navigation callbacks
// so main.cpp only needs to #include this one file.

void onGoToApps();

void onGoToGames();
void onGoToTicTacToe();
void onGoToSnake();
void onGoTo2048();
void onGoToMemoryMatch();
void onGoToChess();

void onGoToOnline();
void onGoToWeather();
void onGoToWikipedia();
void onGoToWordOfDay();
void onGoToHistory();
void onGoToXKCD();

void onGoToAWSCert();
void onGoToAWSPracticeMode(const char* certId);
void onStartAWSQuiz(const char* certId, const char* mode, const char* domain);
