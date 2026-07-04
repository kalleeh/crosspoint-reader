#include "ReadingStatsManager.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <ctime>

namespace {
constexpr char STATS_FILE[] = "/.crosspoint/reading-stats.dat";
constexpr char STATS_TMP[] = "/.crosspoint/reading-stats.tmp";
constexpr uint8_t MAGIC[2] = {0xBD, 0x01};

// Days since epoch when the wall clock is synced, else 0
uint16_t todayDays() {
  time_t now = time(nullptr);
  if (now < 1000000000L) return 0;
  return (uint16_t)(now / 86400);
}
}  // namespace

ReadingStatsManager& ReadingStatsManager::getInstance() {
  static ReadingStatsManager instance;
  return instance;
}

void ReadingStatsManager::load() {
  if (loaded) return;
  loaded = true;

  HalFile file;
  if (!Storage.openFileForRead("RSTAT", STATS_FILE, file)) return;

  uint8_t magic[2];
  if (file.read(magic, 2) != 2 || magic[0] != MAGIC[0] || magic[1] != MAGIC[1]) {
    LOG_ERR("RSTAT", "Bad stats file magic, ignoring");
    file.close();
    return;
  }

  bool ok = file.read((uint8_t*)&totalSeconds, sizeof(totalSeconds)) == sizeof(totalSeconds) &&
            file.read((uint8_t*)&totalPageTurns, sizeof(totalPageTurns)) == sizeof(totalPageTurns) &&
            file.read((uint8_t*)&totalSessions, sizeof(totalSessions)) == sizeof(totalSessions) &&
            file.read((uint8_t*)buckets, sizeof(buckets)) == sizeof(buckets);
  file.close();

  if (!ok) {
    LOG_ERR("RSTAT", "Short read, resetting stats");
    totalSeconds = 0;
    totalPageTurns = 0;
    totalSessions = 0;
    memset(buckets, 0, sizeof(buckets));
  }
}

bool ReadingStatsManager::save() {
  Storage.mkdir("/.crosspoint");

  {
    HalFile file = Storage.open(STATS_TMP, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) {
      LOG_ERR("RSTAT", "Cannot open tmp stats file");
      return false;
    }
    bool ok = file.write(MAGIC, 2) == 2 &&
              file.write((uint8_t*)&totalSeconds, sizeof(totalSeconds)) == sizeof(totalSeconds) &&
              file.write((uint8_t*)&totalPageTurns, sizeof(totalPageTurns)) == sizeof(totalPageTurns) &&
              file.write((uint8_t*)&totalSessions, sizeof(totalSessions)) == sizeof(totalSessions) &&
              file.write((uint8_t*)buckets, sizeof(buckets)) == sizeof(buckets);
    file.close();
    if (!ok) {
      Storage.remove(STATS_TMP);
      LOG_ERR("RSTAT", "Partial stats write, aborted");
      return false;
    }
  }

  // Atomic swap: never leave a torn stats file behind
  Storage.remove(STATS_FILE);
  if (!Storage.rename(STATS_TMP, STATS_FILE)) {
    LOG_ERR("RSTAT", "Rename failed");
    return false;
  }
  return true;
}

void ReadingStatsManager::recordSession(uint32_t seconds, uint32_t pageTurns) {
  if (seconds == 0 && pageTurns == 0) return;
  load();

  totalSeconds += seconds;
  totalPageTurns += pageTurns;
  if (totalSessions < UINT16_MAX) totalSessions++;

  const uint16_t today = todayDays();
  if (today != 0) {
    // Find today's bucket, or replace the oldest one
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < NUM_DAILY_BUCKETS; i++) {
      if (buckets[i].daysSinceEpoch == today) {
        slot = i;
        break;
      }
      if (buckets[i].daysSinceEpoch < buckets[oldest].daysSinceEpoch) oldest = i;
    }
    if (slot < 0) {
      slot = oldest;
      buckets[slot] = {today, 0, 0};
    }
    uint32_t s = buckets[slot].seconds + seconds;
    buckets[slot].seconds = s > UINT16_MAX ? UINT16_MAX : (uint16_t)s;
    uint32_t p = buckets[slot].pageTurns + pageTurns;
    buckets[slot].pageTurns = p > UINT16_MAX ? UINT16_MAX : (uint16_t)p;
  }

  save();
}

uint32_t ReadingStatsManager::getTotalSeconds() {
  load();
  return totalSeconds;
}

uint32_t ReadingStatsManager::getTotalPageTurns() {
  load();
  return totalPageTurns;
}

uint16_t ReadingStatsManager::getTotalSessions() {
  load();
  return totalSessions;
}

ReadingStatsManager::DailyBucket ReadingStatsManager::getToday() {
  load();
  const uint16_t today = todayDays();
  if (today != 0) {
    for (const auto& b : buckets) {
      if (b.daysSinceEpoch == today) return b;
    }
  }
  return {0, 0, 0};
}

int ReadingStatsManager::getRecentDays(DailyBucket* out, int maxDays) {
  load();
  // Copy non-empty buckets, sort most recent first (insertion sort, N=14)
  int count = 0;
  for (const auto& b : buckets) {
    if (b.daysSinceEpoch == 0 || count >= maxDays) continue;
    int pos = count++;
    while (pos > 0 && out[pos - 1].daysSinceEpoch < b.daysSinceEpoch) {
      out[pos] = out[pos - 1];
      pos--;
    }
    out[pos] = b;
  }
  return count;
}
