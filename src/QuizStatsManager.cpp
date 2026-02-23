#include "QuizStatsManager.h"
#include "SDCardManager.h"
#include <time.h>

QuizStatsManager& QuizStatsManager::getInstance() {
  static QuizStatsManager instance;
  return instance;
}

void QuizStatsManager::saveQuizResult(const char* certId, const char* mode, uint8_t score, 
                                      uint8_t total, const std::vector<DomainScore>& domainScores) {
  // Input validation
  if (!certId || !mode || strlen(certId) == 0 || strlen(mode) == 0) {
    Serial.println("[QuizStats] Invalid parameters - certId or mode is null/empty");
    return;
  }
  
  if (total == 0) {
    Serial.println("[QuizStats] Invalid parameters - total questions is 0");
    return;
  }
  
  if (score > total) {
    Serial.printf("[QuizStats] Invalid parameters - score (%d) > total (%d)\n", score, total);
    return;
  }
  
  if (!loaded) loadStats();
  
  // Add new result
  QuizResult result;
  strncpy(result.certId, certId, sizeof(result.certId) - 1);
  result.certId[sizeof(result.certId) - 1] = '\0';  // Ensure null termination
  strncpy(result.mode, mode, sizeof(result.mode) - 1);
  result.mode[sizeof(result.mode) - 1] = '\0';  // Ensure null termination
  
  time_t now = time(nullptr);
  if (now == (time_t)-1) {
    Serial.println("[QuizStats] Time error, using 0");
    now = 0;
  }
  result.timestamp = now;
  result.score = score;
  result.total = total;
  
  results.push_back(result);
  
  // Keep only last 50 results
  if (results.size() > 50) {
    results.erase(results.begin());
  }
  
  // Update domain stats
  for (const auto& ds : domainScores) {
    String key = String(certId) + ":" + String(ds.domain);
    if (domainStats.find(key) == domainStats.end()) {
      domainStats[key] = ds;
    } else {
      domainStats[key].correct += ds.correct;
      domainStats[key].total += ds.total;
    }
  }
  
  updateLastPracticeDate();
  saveStats();
}

int QuizStatsManager::getStreak() {
  if (!loaded) loadStats();
  return calculateStreak();
}

int QuizStatsManager::getTotalQuestionsAnswered() {
  if (!loaded) loadStats();
  
  int total = 0;
  for (const auto& result : results) {
    total += result.total;
  }
  return total;
}

int QuizStatsManager::getAverageScore() {
  if (!loaded) loadStats();
  
  if (results.empty()) return 0;
  
  int totalScore = 0;
  int totalQuestions = 0;
  
  for (const auto& result : results) {
    totalScore += result.score;
    totalQuestions += result.total;
  }
  
  return totalQuestions > 0 ? (totalScore * 100) / totalQuestions : 0;
}

std::vector<QuizStatsManager::QuizResult> QuizStatsManager::getRecentHistory(int limit) {
  if (!loaded) loadStats();
  if (results.empty()) return {};

  std::vector<QuizResult> recent;
  int start = static_cast<int>(results.size()) > limit ? static_cast<int>(results.size()) - limit : 0;

  for (int i = static_cast<int>(results.size()) - 1; i >= start; i--) {
    recent.push_back(results[i]);
  }

  return recent;
}

std::map<String, int> QuizStatsManager::getWeakDomains(int minQuestions) {
  if (!loaded) loadStats();
  
  std::map<String, int> weak;
  
  for (const auto& pair : domainStats) {
    const DomainScore& ds = pair.second;
    if (ds.total >= minQuestions) {
      int percentage = (ds.correct * 100) / ds.total;
      if (percentage < 70) {  // Below 70% is weak
        weak[String(ds.domain)] = percentage;
      }
    }
  }
  
  return weak;
}

void QuizStatsManager::clearAllStats() {
  results.clear();
  domainStats.clear();
  lastPracticeDate = 0;
  saveStats();
}

void QuizStatsManager::loadStats() {
  if (loaded) return;
  
  String path = "/.crosspoint/aws-quiz-stats.dat";
  FsFile file;
  if (!SdMan.openFileForRead("QuizStats", path.c_str(), file)) {
    loaded = true;
    return;
  }
  
  // Read last practice date
  file.read((uint8_t*)&lastPracticeDate, sizeof(lastPracticeDate));
  
  // Read results count
  int resultCount;
  file.read((uint8_t*)&resultCount, sizeof(resultCount));
  
  // Read results
  for (int i = 0; i < resultCount && i < 50; i++) {
    QuizResult result;
    file.read((uint8_t*)&result, sizeof(result));
    results.push_back(result);
  }
  
  // Read domain stats count
  int domainCount;
  file.read((uint8_t*)&domainCount, sizeof(domainCount));
  
  // Read domain stats
  for (int i = 0; i < domainCount && i < 100; i++) {
    char key[96];
    DomainScore ds;
    file.read((uint8_t*)key, sizeof(key));
    file.read((uint8_t*)&ds, sizeof(ds));
    domainStats[String(key)] = ds;
  }
  
  file.close();
  loaded = true;
}

void QuizStatsManager::saveStats() {
  String path = "/.crosspoint/aws-quiz-stats.dat";
  FsFile file = SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    Serial.println("[QuizStats] Failed to save stats");
    return;
  }
  
  // Write last practice date
  file.write((uint8_t*)&lastPracticeDate, sizeof(lastPracticeDate));
  
  // Write results count
  int resultCount = results.size();
  file.write((uint8_t*)&resultCount, sizeof(resultCount));
  
  // Write results
  for (const auto& result : results) {
    file.write((uint8_t*)&result, sizeof(result));
  }
  
  // Write domain stats count
  int domainCount = domainStats.size();
  file.write((uint8_t*)&domainCount, sizeof(domainCount));
  
  // Write domain stats
  for (const auto& pair : domainStats) {
    char key[96];
    strncpy(key, pair.first.c_str(), sizeof(key) - 1);
    file.write((uint8_t*)key, sizeof(key));
    file.write((uint8_t*)&pair.second, sizeof(pair.second));
  }
  
  file.close();
}

void QuizStatsManager::updateLastPracticeDate() {
  lastPracticeDate = time(nullptr);
}

int QuizStatsManager::calculateStreak() {
  if (results.empty()) return 0;
  
  time_t now = time(nullptr);
  time_t lastPractice = lastPracticeDate;
  
  // Check if practiced today or yesterday
  int daysSinceLastPractice = (now - lastPractice) / 86400;  // 86400 seconds in a day
  
  if (daysSinceLastPractice > 1) {
    return 0;  // Streak broken
  }
  
  // Count consecutive days
  int streak = 1;

  for (int i = static_cast<int>(results.size()) - 2; i >= 0; i--) {
    int daysDiff = (lastPractice - results[i].timestamp) / 86400;
    
    if (daysDiff == streak) {
      streak++;
      lastPractice = results[i].timestamp;
    } else if (daysDiff > streak) {
      break;  // Gap in streak
    }
  }
  
  return streak;
}
