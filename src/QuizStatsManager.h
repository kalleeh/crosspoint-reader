#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

class QuizStatsManager {
 public:
  struct QuizResult {
    char certId[32];
    char mode[16];
    uint32_t timestamp;
    uint8_t score;
    uint8_t total;
  };
  
  struct DomainScore {
    char domain[64];
    uint8_t correct;
    uint8_t total;
  };
  
  static QuizStatsManager& getInstance();
  
  // Save quiz result
  void saveQuizResult(const char* certId, const char* mode, uint8_t score, uint8_t total,
                      const std::vector<DomainScore>& domainScores);
  
  // Get stats
  int getStreak();
  int getTotalQuestionsAnswered();
  int getAverageScore();
  std::vector<QuizResult> getRecentHistory(int limit = 5);
  std::map<String, int> getWeakDomains(int minQuestions = 5);
  
  // Clear data
  void clearAllStats();
  
 private:
  QuizStatsManager() = default;
  QuizStatsManager(const QuizStatsManager&) = delete;
  QuizStatsManager& operator=(const QuizStatsManager&) = delete;
  
  void loadStats();
  void saveStats();
  void updateLastPracticeDate();
  int calculateStreak();
  
  std::vector<QuizResult> results;
  std::map<String, DomainScore> domainStats;
  uint32_t lastPracticeDate = 0;
  bool loaded = false;
};
