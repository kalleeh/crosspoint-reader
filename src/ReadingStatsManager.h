#pragma once

#include <cstdint>

// Fork: tracks reading time and page turns across sessions.
// Persisted to /.crosspoint/reading-stats.dat (atomic tmp+rename, same
// pattern as QuizStatsManager). Daily buckets are only recorded when the
// wall clock is synced (NTP/RTC); lifetime totals always accumulate.
class ReadingStatsManager {
 public:
#pragma pack(push, 1)
  struct DailyBucket {
    uint16_t daysSinceEpoch;  // 0 = empty slot
    uint16_t seconds;         // capped at 65535 (~18h/day)
    uint16_t pageTurns;
  };
#pragma pack(pop)

  static constexpr int NUM_DAILY_BUCKETS = 14;

  static ReadingStatsManager& getInstance();

  // Called by the reader on session end. Accumulates and saves.
  void recordSession(uint32_t seconds, uint32_t pageTurns);

  // Queries (load on demand)
  uint32_t getTotalSeconds();
  uint32_t getTotalPageTurns();
  uint16_t getTotalSessions();
  // Bucket for today / recent days; returns empty bucket if clock unsynced
  DailyBucket getToday();
  // Fills out[0..NUM_DAILY_BUCKETS-1], most recent first; returns count filled
  int getRecentDays(DailyBucket* out, int maxDays);

 private:
  ReadingStatsManager() = default;
  ReadingStatsManager(const ReadingStatsManager&) = delete;
  ReadingStatsManager& operator=(const ReadingStatsManager&) = delete;

  void load();
  bool save();

  uint32_t totalSeconds = 0;
  uint32_t totalPageTurns = 0;
  uint16_t totalSessions = 0;
  DailyBucket buckets[NUM_DAILY_BUCKETS] = {};
  bool loaded = false;
};

#define READING_STATS ReadingStatsManager::getInstance()
