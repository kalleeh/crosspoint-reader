#include "QuizStatsManager.h"
#include <HalStorage.h>
#include <time.h>
#include <algorithm>

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
  while (results.size() > 50) {
    results.erase(results.begin());
  }
  
  // Update domain stats (cap at 100 entries to bound memory usage)
  for (const auto& ds : domainScores) {
    String key = String(certId) + ":" + String(ds.domain);
    if (domainStats.find(key) != domainStats.end()) {
      domainStats[key].correct += ds.correct;
      domainStats[key].total += ds.total;
    } else if (domainStats.size() < 100) {
      domainStats[key] = ds;
    }
  }

  updateLastPracticeDate(certId);
  saveStats();
}

int QuizStatsManager::getStreak(const char* certId) {
  if (!loaded) loadStats();
  return calculateStreak(certId);
}

int QuizStatsManager::getTotalQuestionsAnswered(const char* certId) {
  if (!loaded) loadStats();

  bool filterByCert = (certId != nullptr && certId[0] != '\0');
  int total = 0;
  for (const auto& result : results) {
    if (filterByCert && strcmp(result.certId, certId) != 0) continue;
    total += result.total;
  }
  return total;
}

int QuizStatsManager::getAverageScore(const char* certId) {
  if (!loaded) loadStats();

  if (results.empty()) return 0;

  bool filterByCert = (certId != nullptr && certId[0] != '\0');
  int totalScore = 0;
  int totalQuestions = 0;

  for (const auto& result : results) {
    if (filterByCert && strcmp(result.certId, certId) != 0) continue;
    totalScore += result.score;
    totalQuestions += result.total;
  }

  return totalQuestions > 0 ? ((long)totalScore * 100) / totalQuestions : 0;
}

std::vector<QuizStatsManager::QuizResult> QuizStatsManager::getRecentHistory(int limit, const char* certId) {
  if (!loaded) loadStats();
  if (results.empty()) return {};

  bool filterByCert = (certId != nullptr && certId[0] != '\0');
  std::vector<QuizResult> history;
  int count = 0;
  for (int i = (int)results.size() - 1; i >= 0 && count < limit; i--) {
    if (filterByCert && strcmp(results[i].certId, certId) != 0) continue;
    history.push_back(results[i]);
    count++;
  }
  return history;
}

std::map<String, int> QuizStatsManager::getWeakDomains(int minQuestions, const char* certId) {
  if (!loaded) loadStats();

  std::map<String, int> weak;

  // Build prefix filter when certId is provided
  String prefix;
  bool filterByCert = (certId != nullptr && certId[0] != '\0');
  if (filterByCert) {
    prefix = String(certId) + ":";
  }

  for (const auto& pair : domainStats) {
    if (filterByCert && !pair.first.startsWith(prefix)) {
      continue;
    }
    const DomainScore& ds = pair.second;
    if (ds.total >= minQuestions) {
      if (ds.total == 0) continue;
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
  lastPracticeDateByCert.clear();
  loaded = false;
  saveStats();
}

void QuizStatsManager::loadStats() {
  if (loaded) return;

  const char* path = "/.crosspoint/aws-quiz-stats.dat";
  HalFile file;
  if (!Storage.openFileForRead("QuizStats", path, file)) {
    loaded = true;
    return;
  }

  // Read per-cert lastPracticeDate map
  int certDateCount;
  if (file.read((uint8_t*)&certDateCount, sizeof(certDateCount)) != sizeof(certDateCount)
      || certDateCount < 0 || certDateCount > 200) {
    file.close(); loaded = true; return;
  }
  for (int i = 0; i < certDateCount; i++) {
    char certKey[32] = {};
    uint32_t dateVal = 0;
    if (file.read((uint8_t*)certKey, sizeof(certKey)) != sizeof(certKey)) {
      file.close(); loaded = true; return;
    }
    certKey[31] = '\0';
    if (file.read((uint8_t*)&dateVal, sizeof(dateVal)) != sizeof(dateVal)) {
      file.close(); loaded = true; return;
    }
    lastPracticeDateByCert[String(certKey)] = dateVal;
  }

  // Read results count
  int resultCount;
  if (file.read((uint8_t*)&resultCount, sizeof(resultCount)) != sizeof(resultCount)
      || resultCount < 0 || resultCount > 50) {
    file.close(); loaded = true; return;
  }

  // Read results
  for (int i = 0; i < resultCount; i++) {
    QuizResult result;
    if (file.read((uint8_t*)&result, sizeof(result)) != sizeof(result)) {
      file.close(); loaded = true; return;
    }
    results.push_back(result);
  }

  // Read domain stats count
  int domainCount;
  if (file.read((uint8_t*)&domainCount, sizeof(domainCount)) != sizeof(domainCount)
      || domainCount < 0 || domainCount > 100) {
    file.close(); loaded = true; return;
  }

  // Read domain stats
  for (int i = 0; i < domainCount; i++) {
    char key[96];
    DomainScore ds;
    if (file.read((uint8_t*)key, sizeof(key)) != sizeof(key)) {
      file.close(); loaded = true; return;
    }
    key[95] = '\0';
    if (file.read((uint8_t*)&ds, sizeof(ds)) != sizeof(ds)) {
      file.close(); loaded = true; return;
    }
    domainStats[String(key)] = ds;
  }

  file.close();
  loaded = true;
}

void QuizStatsManager::saveStats() {
  const char* tmpPath = "/.crosspoint/aws-quiz-stats.tmp";
  const char* realPath = "/.crosspoint/aws-quiz-stats.dat";

  // Write to temp file first — avoids corrupting the real file on power loss
  HalFile file = Storage.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    Serial.println("[QuizStats] Failed to open tmp file for save");
    return;
  }

  bool writeOk = true;

  // Write per-cert lastPracticeDate map
  int certDateCount = (int)lastPracticeDateByCert.size();
  writeOk &= (file.write((uint8_t*)&certDateCount, sizeof(certDateCount)) == sizeof(certDateCount));
  for (const auto& pair : lastPracticeDateByCert) {
    char certKey[32] = {};
    strncpy(certKey, pair.first.c_str(), sizeof(certKey) - 1);
    writeOk &= (file.write((uint8_t*)certKey, sizeof(certKey)) == sizeof(certKey));
    writeOk &= (file.write((uint8_t*)&pair.second, sizeof(pair.second)) == sizeof(pair.second));
  }

  // Write results count + results
  int resultCount = (int)results.size();
  writeOk &= (file.write((uint8_t*)&resultCount, sizeof(resultCount)) == sizeof(resultCount));
  for (const auto& result : results) {
    writeOk &= (file.write((uint8_t*)&result, sizeof(result)) == sizeof(result));
  }

  // Write domain stats count + entries
  int domainCount = (int)domainStats.size();
  writeOk &= (file.write((uint8_t*)&domainCount, sizeof(domainCount)) == sizeof(domainCount));
  for (const auto& pair : domainStats) {
    char key[96] = {};
    strncpy(key, pair.first.c_str(), sizeof(key) - 1);
    writeOk &= (file.write((uint8_t*)key, sizeof(key)) == sizeof(key));
    writeOk &= (file.write((uint8_t*)&pair.second, sizeof(pair.second)) == sizeof(pair.second));
  }

  if (!writeOk) {
    Serial.println("[QuizStats] Write failed, aborting save");
    file.close();
    Storage.remove(tmpPath);
    return;
  }

  file.close();

  // Atomic swap: rename tmp -> real.
  // Try rename first (FAT rename may fail if destination exists).
  // Only remove the old file if rename without it fails — this way
  // the old .dat is never deleted before the new data is ready.
  if (!Storage.rename(tmpPath, realPath)) {
    Storage.remove(realPath);
    if (!Storage.rename(tmpPath, realPath)) {
      Serial.println("[QuizStats] rename failed, removing tmp");
      Storage.remove(tmpPath);
    }
  }
}

void QuizStatsManager::updateLastPracticeDate(const char* certId) {
  time_t now = time(nullptr);
  if (now == (time_t)-1) now = 0;  // Match saveQuizResult() error handling
  lastPracticeDateByCert[String(certId)] = (uint32_t)now;
}

static int toLocalDay(time_t t) {
  if (t <= 0) return -1;  // clock not set or invalid
  // Days since epoch (UTC). Correct across leap years, good enough for streak tracking.
  return (int)(t / 86400);
}

int QuizStatsManager::calculateStreak(const char* certId) {
  if (results.empty()) return 0;

  bool filterByCert = (certId != nullptr && certId[0] != '\0');

  // Build a filtered view of results for this cert
  std::vector<const QuizResult*> filtered;
  for (const auto& r : results) {
    if (!filterByCert || strcmp(r.certId, certId) == 0) {
      filtered.push_back(&r);
    }
  }

  if (filtered.empty()) return 0;

  // Sort oldest→newest so the streak walk is correct regardless of insertion order
  std::sort(filtered.begin(), filtered.end(), [](const QuizResult* a, const QuizResult* b) {
    return a->timestamp < b->timestamp;
  });

  time_t now = time(nullptr);
  if (now <= 0) return 0;  // clock not set — don't guess streak

  // Use per-cert last practice date; fall back to most recent across all certs
  uint32_t storedDate = 0;
  if (filterByCert) {
    auto it = lastPracticeDateByCert.find(String(certId));
    if (it != lastPracticeDateByCert.end()) storedDate = it->second;
  } else {
    for (const auto& pair : lastPracticeDateByCert) {
      if (pair.second > storedDate) storedDate = pair.second;
    }
  }
  time_t lastPractice = (time_t)storedDate;

  // Check if practiced today or yesterday
  int daysSinceLastPractice = toLocalDay(now) - toLocalDay(lastPractice);
  if (daysSinceLastPractice < 0) return 0;  // clock went backward — safe reset rather than invalid streak

  if (daysSinceLastPractice > 1) {
    return 0;  // Streak broken
  }

  // Count consecutive days — each step back must be exactly 1 day apart
  int streak = 1;

  for (int i = static_cast<int>(filtered.size()) - 2; i >= 0; i--) {
    int daysDiff = toLocalDay(lastPractice) - toLocalDay(filtered[i]->timestamp);

    if (daysDiff == 1) {
      streak++;
      lastPractice = filtered[i]->timestamp;
    } else {
      break;  // Any gap other than exactly 1 day breaks the streak
    }
  }

  return streak;
}
