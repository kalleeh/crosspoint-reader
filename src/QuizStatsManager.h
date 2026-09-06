#pragma once

#include <Arduino.h>

#include <map>
#include <vector>

class QuizStatsManager {
 public:
#pragma pack(push, 1)
  struct QuizResult {
    char certId[32];
    char mode[16];
    uint32_t timestamp;
    uint8_t score;
    uint8_t total;
  };

  struct DomainScore {
    char domain[64];
    uint16_t correct;
    uint16_t total;
  };
#pragma pack(pop)

  static QuizStatsManager& getInstance();

  // Save quiz result
  void saveQuizResult(const char* certId, const char* mode, uint8_t score, uint8_t total,
                      const std::vector<DomainScore>& domainScores);

  // Get stats
  int getStreak(const char* certId = nullptr);
  int getTotalQuestionsAnswered(const char* certId = nullptr);
  int getAverageScore(const char* certId = nullptr);
  std::vector<QuizResult> getRecentHistory(int limit = 5, const char* certId = nullptr);
  std::map<String, int> getWeakDomains(int minQuestions = 5, const char* certId = nullptr);

  // Clear data
  void clearAllStats();

 private:
  QuizStatsManager() = default;
  QuizStatsManager(const QuizStatsManager&) = delete;
  QuizStatsManager& operator=(const QuizStatsManager&) = delete;

  void loadStats();
  void saveStats();
  void updateLastPracticeDate(const char* certId);
  int calculateStreak(const char* certId = nullptr);

  std::vector<QuizResult> results;
  std::map<String, DomainScore> domainStats;
  std::map<String, uint32_t> lastPracticeDateByCert;
  bool loaded = false;
};
