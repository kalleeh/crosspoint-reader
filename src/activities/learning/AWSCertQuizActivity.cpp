#include "components/UITheme.h"
#include "../../DebugConfig.h"
#include "AWSCertQuizActivity.h"
#include "../../fontIds.h"
#include "../../QuizStatsManager.h"
#include <HalDisplay.h>
#include <HalStorage.h>
#include <ArduinoJson.h>
#include <I18n.h>
#include <ForkI18n.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <random>
#include <esp_random.h>
#include <map>

// Sanitize a string for use as a FAT filename component.
// Replaces any character that isn't alphanumeric, '-', or '_' with '_'.
static void sanitizeFilenameComponent(const char* src, char* dst, size_t dstSize) {
  size_t i = 0;
  for (; src[i] && i < dstSize - 1; i++) {
    char c = src[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      dst[i] = c;
    } else {
      dst[i] = '_';
    }
  }
  dst[i] = '\0';
}

// Constants
static constexpr int MAX_QUESTIONS_FROM_FILE = 200;  // Maximum questions to load from a single JSON file
static constexpr size_t JSON_PARSE_BUF_SIZE = 2200;
static constexpr size_t PATH_BUF_SIZE = 128;
static constexpr int DEFAULT_MARGIN = 20;
static constexpr int MIN_OPTION_HEIGHT = 30;

// All questions loaded from SD card JSON files
// Place JSON files in /aws-quiz/{cert-id}.json on SD card

void AWSCertQuizActivity::onEnter() {
  // Reset per-quiz state before anything else so retries always start clean
  correctCount = 0;
  currentStreak = 0;
  longestStreak = 0;
  incorrectQuestions.clear();
  domainCorrect.clear();
  domainTotal.clear();
  endTime = 0;

  // Show loading screen while parsing (JSON parse can take several seconds)
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, DEFAULT_MARGIN, renderer.getScreenHeight() / 2 - 10, fork_tr(STR_AWS_LOADING_QUESTIONS), true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  // ------------------------------------------------------------------
  // Phase 1: Determine which file-sequential question indices to load.
  // This happens BEFORE any file parsing so we only pull the questions
  // we actually need into RAM, preventing OOM crashes on large banks.
  // ------------------------------------------------------------------
  std::vector<uint16_t> fileIndices;

  // How many questions this mode expects (used both to validate a resumable
  // session and to size a fresh start).
  int neededCount;
  if      (practiceMode == "quick")      neededCount = 15;
  else if (practiceMode == "quickstart") neededCount = 5;
  else if (practiceMode == "study")      neededCount = 20;
  else                                   neededCount = getMaxQuestionsForCert();

  if (practiceMode == "review") {
    // Review mode: load only the questions that were previously wrong.
    if (!loadIncorrectHistory(fileIndices) || fileIndices.empty()) {
      showError(fork_tr(STR_AWS_ERR_NO_REVIEW_TITLE), fork_tr(STR_AWS_ERR_NO_REVIEW_BODY));
      return;
    }
    std::sort(fileIndices.begin(), fileIndices.end());

  } else {
    // Domain mode: the bank may hold fewer questions for a domain than the
    // mode's nominal size, so scan first and clamp neededCount to what
    // actually exists. Otherwise a valid saved domain session could never
    // pass the resume size check below.
    std::vector<uint16_t> domainIndices;
    if (practiceMode == "domain" && practiceDomain != "all") {
      if (!cheapDomainScan(practiceDomain.c_str(), domainIndices) || domainIndices.empty()) {
        showError(fork_tr(STR_AWS_ERR_NO_QUESTIONS_TITLE), fork_tr(STR_AWS_ERR_NO_DOMAIN_BODY));
        return;
      }
      neededCount = std::min(neededCount, (int)domainIndices.size());
    }

    if (practiceMode != "quickstart" && peekSessionFileIndices(fileIndices) &&
        (int)fileIndices.size() == neededCount) {
      // Resuming an interrupted session — reload the exact same question set.
      // Only resume if the saved session has the number of questions this mode
      // expects. A mismatch means the session is stale/degenerate (e.g. a leftover
      // 2-question file for a 65-question Full exam), so fall through to a fresh
      // start instead of resuming into a broken quiz.
      // loadCustomQuestions() requires a sorted index list (forward-only cursor);
      // peekSessionFileIndices returns them in shuffled questionOrder sequence.
      std::sort(fileIndices.begin(), fileIndices.end());

    } else {
      // Fresh start.
      fileIndices.clear();

      if (practiceMode == "domain" && practiceDomain != "all") {
        // If more domain questions exist than we need, sub-select with
        // spaced-repetition weighting (previously-missed questions favored).
        if ((int)domainIndices.size() > neededCount) {
          std::vector<uint16_t> sub;
          weightedSelectIndices(domainIndices.size(), neededCount, sub, domainIndices.data());
          fileIndices.reserve(sub.size());
          for (uint16_t idx : sub) fileIndices.push_back(domainIndices[idx]);
          std::sort(fileIndices.begin(), fileIndices.end());
        } else {
          // cheapDomainScan appends in file order, so already sorted.
          fileIndices = std::move(domainIndices);
        }
      } else {
        // Selection from the whole bank with spaced-repetition weighting.
        int total = countQuestionsInFile();
        if (total <= 0) {
          showError(fork_tr(STR_AWS_ERR_NO_QUESTIONS_TITLE), fork_tr(STR_AWS_ERR_NO_FILES_BODY));
          return;
        }
        weightedSelectIndices(total, neededCount, fileIndices);
      }
    }
  }

  // ------------------------------------------------------------------
  // Phase 2: Load only the selected questions from the SD card.
  // ------------------------------------------------------------------
  hasCustomPack = loadCustomQuestions(&fileIndices);

  if (!hasCustomPack || customQuestions.empty()) {
    state = LOADING;
    showError(fork_tr(STR_AWS_ERR_NO_QUESTIONS_TITLE), fork_tr(STR_AWS_ERR_NO_FILES_BODY));
    return;
  }

  // Set max questions based on mode
  if (practiceMode == "quick") {
    maxQuestions = 15;
  } else if (practiceMode == "study") {
    maxQuestions = 20;
  } else if (practiceMode == "quickstart") {
    maxQuestions = 5;
  } else if (practiceMode == "review") {
    maxQuestions = (int)customQuestions.size();
  } else {
    maxQuestions = getMaxQuestionsForCert();
  }

  // Enable timer for Full Exam mode
  showTimer = (practiceMode == "full");
  if (showTimer) {
    startTime = millis();
  }

  // Try to resume an interrupted session.
  if (practiceMode != "review" && practiceMode != "quickstart" && loadSession()) {
    for (int i = 0; i < currentIndex && i < (int)userAnswers.size(); i++) {
      if (userAnswers[i] >= 0) {
        int aIdx = questionOrder[i];
        if (aIdx < (int)customQuestions.size() && userAnswers[i] == customQuestions[aIdx].correct) {
          correctCount++;
        }
      }
    }
    DEBUG_PRINTLN("[AWS] Resumed previous session");
    state = QUESTION;
    renderQuestion();
    return;
  }

  // Start new session.
  if (practiceMode == "review") {
    // Build a sequential questionOrder over the densely-loaded customQuestions.
    questionOrder.clear();
    questionCount = (int)customQuestions.size();
    questionOrder.reserve(questionCount);
    for (int i = 0; i < questionCount; i++) questionOrder.push_back((uint16_t)i);
    userAnswers.clear();
    userAnswers.resize(questionCount, -1);
  } else {
    loadQuestions();
    shuffleQuestions();
    if (questionOrder.empty() || questionCount == 0) {
      state = LOADING;
      showError(fork_tr(STR_AWS_ERR_NO_QUESTIONS_TITLE), fork_tr(STR_AWS_ERR_NO_DOMAIN_BODY));
      return;
    }
  }
  state = QUESTION;
  renderQuestion();
}

void AWSCertQuizActivity::loadQuestions() {
  questionCount = customQuestions.size();

  // Filter by domain if in domain mode (use indices, don't copy)
  if (practiceMode == "domain" && practiceDomain != "all") {
    std::vector<uint16_t> filteredIndices;
    for (uint16_t i = 0; i < customQuestions.size(); i++) {
      if (strcasecmp(customQuestions[i].domain.c_str(), practiceDomain.c_str()) == 0) {
        filteredIndices.push_back(i);
      }
    }

    // Update questionOrder to only include filtered indices
    questionOrder = filteredIndices;
    questionCount = filteredIndices.size();
    DEBUG_PRINTF("[AWS] Filtered to %d questions for domain: %s\n", questionCount, practiceDomain.c_str());
    if (filteredIndices.empty()) {
      // Caller (onEnter) will detect questionCount == 0 and show an error
      return;
    }
    return;
  }
}

void AWSCertQuizActivity::shuffleQuestions() {
  // Only build questionOrder from scratch if not already populated (e.g. by domain filtering)
  if (questionOrder.empty()) {
    questionOrder.reserve(questionCount);
    for (int i = 0; i < questionCount; i++) {
      questionOrder.push_back(i);
    }
  }

  // Always reinitialise userAnswers
  userAnswers.clear();
  userAnswers.resize(questionOrder.size(), -1);
  questionCount = static_cast<int>(questionOrder.size());

  // Shuffle using ESP32 hardware RNG — std::random_device returns the same
  // value every boot on ESP32, so we use esp_random() for true randomness.
  std::seed_seq seed{esp_random(), esp_random(), esp_random()};
  std::mt19937 g(seed);
  std::shuffle(questionOrder.begin(), questionOrder.end(), g);

  // Limit to maxQuestions if bank is larger
  if (questionCount > maxQuestions) {
    questionOrder.resize(maxQuestions);
    userAnswers.resize(maxQuestions);
    questionCount = maxQuestions;
  }

  saveSession();  // Save new session
}

void AWSCertQuizActivity::saveSession() {
  // Review and quickstart modes don't need session persistence.
  if (practiceMode == "review" || practiceMode == "quickstart") return;

  char safeDomain[48];
  sanitizeFilenameComponent(practiceDomain.c_str(), safeDomain, sizeof(safeDomain));
  char path[256];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s-%s-%s.dat", certId.c_str(), practiceMode.c_str(), safeDomain);
  HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;

  bool ok = true;

  // Write session data (v2 format):
  //   [currentIndex: int32][questionCount: int32]
  //   [fileIndices: uint16 × N][questionOrder: uint16 × N][userAnswers: int8 × N]
  ok = ok && file.write((uint8_t*)&currentIndex, sizeof(currentIndex)) == sizeof(currentIndex);
  ok = ok && file.write((uint8_t*)&questionCount, sizeof(questionCount)) == sizeof(questionCount);

  // Write fileIndices so the same question set can be re-loaded on resume
  for (int i = 0; ok && i < questionCount; i++) {
    uint16_t fi = (questionOrder[i] < customQuestions.size())
                  ? customQuestions[questionOrder[i]].fileIndex : 0;
    ok = ok && file.write((uint8_t*)&fi, sizeof(fi)) == sizeof(fi);
  }

  // Write questionOrder (native uint16_t, no widening)
  for (int i = 0; ok && i < questionCount; i++) {
    uint16_t val = questionOrder[i];
    ok = ok && file.write((uint8_t*)&val, sizeof(val)) == sizeof(val);
  }

  // Write userAnswers (native int8_t, no widening)
  for (int i = 0; ok && i < questionCount; i++) {
    int8_t val = userAnswers[i];
    ok = ok && file.write((uint8_t*)&val, sizeof(val)) == sizeof(val);
  }

  file.close();

  // Remove corrupted file on partial write
  if (!ok) {
    Storage.remove(path);
    DEBUG_PRINTLN("[AWS] saveSession: partial write, removed corrupted file");
  }
}

bool AWSCertQuizActivity::loadSession() {
  char safeDomain[48];
  sanitizeFilenameComponent(practiceDomain.c_str(), safeDomain, sizeof(safeDomain));
  char path[256];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s-%s-%s.dat", certId.c_str(), practiceMode.c_str(), safeDomain);
  HalFile file;
  if (!openFileOrShowError(path, file, fork_tr(STR_AWS_ERR_SESSION_TITLE))) {
    return false;
  }

  // Read session data with validation
  if (file.read((uint8_t*)&currentIndex, sizeof(currentIndex)) != sizeof(currentIndex) ||
      file.read((uint8_t*)&questionCount, sizeof(questionCount)) != sizeof(questionCount)) {
    file.close();
    return false;
  }

  // Validate bounds
  if (questionCount <= 0 || questionCount > MAX_QUESTIONS_FROM_FILE ||
      currentIndex < 0 || currentIndex >= questionCount) {
    file.close();
    return false;
  }

  // v2 format: skip the fileIndices section (already used during pre-load phase)
  // Expected remaining bytes: fileIndices(N×2) + questionOrder(N×2) + userAnswers(N×1)
  size_t expectedRemaining = (size_t)questionCount * (sizeof(uint16_t) + sizeof(uint16_t) + sizeof(int8_t));
  if (file.size() < (size_t)(8 + expectedRemaining)) {
    // Old format session (no fileIndices) — discard so we start fresh
    file.close();
    return false;
  }
  for (int i = 0; i < questionCount; i++) {
    uint16_t dummy;
    if (file.read((uint8_t*)&dummy, sizeof(dummy)) != sizeof(dummy)) {
      file.close(); return false;
    }
  }

  // Read questionOrder (stored as uint16_t)
  questionOrder.clear();
  questionOrder.reserve(questionCount);
  for (int i = 0; i < questionCount; i++) {
    uint16_t val;
    if (file.read((uint8_t*)&val, sizeof(val)) != sizeof(val)) {
      file.close();
      return false;
    }
    if (val >= (uint16_t)customQuestions.size()) {
      file.close();
      return false;
    }
    questionOrder.push_back(val);
  }

  // Read userAnswers (stored as int8_t)
  userAnswers.clear();
  userAnswers.reserve(questionCount);
  for (int i = 0; i < questionCount; i++) {
    int8_t val;
    if (file.read((uint8_t*)&val, sizeof(val)) != sizeof(val)) {
      file.close();
      return false;
    }
    userAnswers.push_back(val);
  }

  file.close();

  // Restore selectedOption if current question was answered
  if (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0) {
    selectedOption = userAnswers[currentIndex];
  }

  return true;
}

void AWSCertQuizActivity::clearSession() {
  char safeDomain[48];
  sanitizeFilenameComponent(practiceDomain.c_str(), safeDomain, sizeof(safeDomain));
  char path[256];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s-%s-%s.dat", certId.c_str(), practiceMode.c_str(), safeDomain);
  Storage.remove(path);
}

// ---------------------------------------------------------------------------
// History file (spaced repetition), /.crosspoint/aws-quiz-history-{cert}.dat
// v3: [magic 0xAB 0x03][count: uint16][{fileIndex: uint16, missCount: uint8,
//     correctStreak: uint8} × count]
// v2 (read-only migration): [magic 0xAB 0x02][count][fileIndex × count]
// Records MERGE across quizzes: a miss bumps missCount and resets the streak;
// a correct answer on a tracked question bumps the streak, and two correct
// answers in a row "graduate" the record out of the file.
// ---------------------------------------------------------------------------
bool AWSCertQuizActivity::loadHistoryRecords(std::vector<HistoryRecord>& outRecords) {
  outRecords.clear();

  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-history-%s.dat", certId.c_str());
  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTLN("[AWS] No incorrect question history found");
    return false;
  }

  uint8_t magic[2];
  if (file.read(magic, 2) != 2 || magic[0] != 0xAB || (magic[1] != 0x02 && magic[1] != 0x03)) {
    DEBUG_PRINTLN("[AWS] History file is old format — discarding");
    file.close();
    return false;
  }
  const bool v3 = (magic[1] == 0x03);

  uint16_t count;
  if (file.read((uint8_t*)&count, sizeof(count)) != sizeof(count) || count == 0 || count > MAX_HISTORY_RECORDS) {
    file.close();
    return false;
  }

  outRecords.reserve(count);
  for (int i = 0; i < (int)count; i++) {
    HistoryRecord rec{0, 1, 0};
    if (file.read((uint8_t*)&rec.fileIndex, sizeof(rec.fileIndex)) != sizeof(rec.fileIndex)) {
      file.close();
      outRecords.clear();
      return false;
    }
    if (v3) {
      if (file.read(&rec.missCount, 1) != 1 || file.read(&rec.correctStreak, 1) != 1) {
        file.close();
        outRecords.clear();
        return false;
      }
    }
    // v2 records migrate as missCount=1, streak=0 (set at initialisation)
    outRecords.push_back(rec);
  }

  file.close();
  DEBUG_PRINTF("[AWS] Loaded %d history records (%s)\n", count, v3 ? "v3" : "v2-migrated");
  return true;
}

void AWSCertQuizActivity::saveIncorrectHistory() {
  // Merge this quiz's outcomes into the persistent records.
  std::vector<HistoryRecord> records;
  loadHistoryRecords(records);
  if (records.empty() && incorrectQuestions.empty()) return;  // nothing tracked, nothing missed

  auto findRecord = [&records](uint16_t fi) -> HistoryRecord* {
    for (auto& r : records) {
      if (r.fileIndex == fi) return &r;
    }
    return nullptr;
  };

  // Pass 1: misses — bump missCount, reset streak, create record if new
  for (int idx : incorrectQuestions) {
    int actualIdx = questionOrder[idx];
    if (actualIdx < 0 || actualIdx >= (int)customQuestions.size()) continue;
    const uint16_t fi = customQuestions[actualIdx].fileIndex;
    if (HistoryRecord* rec = findRecord(fi)) {
      if (rec->missCount < MAX_MISS_COUNT) rec->missCount++;
      rec->correctStreak = 0;
    } else {
      records.push_back({fi, 1, 0});
    }
  }

  // Pass 2: correct answers on tracked questions — bump streak, graduate at 2
  for (int i = 0; i < questionCount; i++) {
    if (i >= (int)userAnswers.size() || userAnswers[i] < 0) continue;
    const int actualIdx = questionOrder[i];
    if (actualIdx < 0 || actualIdx >= (int)customQuestions.size()) continue;
    const Question& q = customQuestions[actualIdx];
    if (userAnswers[i] != q.correct) continue;  // misses handled in pass 1
    if (HistoryRecord* rec = findRecord(q.fileIndex)) {
      rec->correctStreak++;
    }
  }
  records.erase(std::remove_if(records.begin(), records.end(),
                               [](const HistoryRecord& r) { return r.correctStreak >= GRADUATE_STREAK; }),
                records.end());

  // Cap: drop the least-missed records first
  if (records.size() > MAX_HISTORY_RECORDS) {
    std::sort(records.begin(), records.end(),
              [](const HistoryRecord& a, const HistoryRecord& b) { return a.missCount > b.missCount; });
    records.resize(MAX_HISTORY_RECORDS);
  }

  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-history-%s.dat", certId.c_str());
  if (records.empty()) {
    // Everything graduated — remove the file rather than writing an empty one
    Storage.remove(path);
    DEBUG_PRINTLN("[AWS] History empty after merge, removed file");
    return;
  }

  HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;

  uint8_t magic[2] = {0xAB, 0x03};
  file.write(magic, 2);
  uint16_t count = (uint16_t)records.size();
  file.write((uint8_t*)&count, sizeof(count));
  for (const auto& rec : records) {
    file.write((uint8_t*)&rec.fileIndex, sizeof(rec.fileIndex));
    file.write(&rec.missCount, 1);
    file.write(&rec.correctStreak, 1);
  }
  file.close();
  DEBUG_PRINTF("[AWS] Saved %d history records (v3)\n", count);
}

bool AWSCertQuizActivity::loadIncorrectHistory(std::vector<uint16_t>& outFileIndices) {
  outFileIndices.clear();
  std::vector<HistoryRecord> records;
  if (!loadHistoryRecords(records)) return false;

  outFileIndices.reserve(records.size());
  for (const auto& rec : records) outFileIndices.push_back(rec.fileIndex);
  return !outFileIndices.empty();
}

bool AWSCertQuizActivity::openFileOrShowError(const char* path, HalFile& file, const char* errorTitle) {
  if (!Storage.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTF("[AWS] Failed to open: %s\n", path);
    showError(errorTitle, fork_tr(STR_AWS_ERR_FILE_BODY));
    return false;
  }
  return true;
}

uint16_t AWSCertQuizActivity::getCachedTextWidth(int fontId, const char* text) const {
  // Check cache
  for (int i = 0; i < 4; i++) {
    if (!textWidthCache[i].text.empty() && textWidthCache[i].text == text) {
      return textWidthCache[i].width;
    }
  }
  
  // Calculate and cache (LRU: shift and add to end)
  uint16_t width = renderer.getTextWidth(fontId, text);
  for (int i = 0; i < 3; i++) {
    textWidthCache[i] = textWidthCache[i + 1];
  }
  textWidthCache[3].text = std::string(text);
  textWidthCache[3].width = width;
  
  return width;
}

void AWSCertQuizActivity::saveQuizStats() {
  // Re-use domainCorrect/domainTotal already computed at quiz end — no need to re-scan
  std::vector<QuizStatsManager::DomainScore> domainVec;
  for (const auto& pair : domainTotal) {
    const String& domainKey = pair.first;
    QuizStatsManager::DomainScore ds;
    strncpy(ds.domain, domainKey.c_str(), sizeof(ds.domain) - 1);
    ds.domain[sizeof(ds.domain) - 1] = '\0';
    ds.total = pair.second;
    auto it = domainCorrect.find(domainKey);
    ds.correct = (it != domainCorrect.end()) ? it->second : 0;
    domainVec.push_back(ds);
  }
  
  // Calculate total score (only answered questions)
  int totalCorrect = 0;
  int totalAnswered = 0;
  for (int i = 0; i < questionCount; i++) {
    if (userAnswers[i] < 0) continue;
    totalAnswered++;
    int actualIdx = questionOrder[i];
    if (userAnswers[i] == customQuestions[actualIdx].correct) {
      totalCorrect++;
    }
  }

  // Save to stats manager
  if (totalAnswered == 0) return;  // Nothing to save if no questions were answered

  QuizStatsManager::getInstance().saveQuizResult(
    certId.c_str(),
    practiceMode.c_str(),
    totalCorrect,
    totalAnswered,
    domainVec
  );

  DEBUG_PRINTF("[AWS] Saved quiz stats: %d/%d correct (%d unanswered)\n",
               totalCorrect, totalAnswered, questionCount - totalAnswered);
}

void AWSCertQuizActivity::showError(const char* title, const char* message) {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();
  
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, title, true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  
  renderer.drawLine(margin, y, width - margin, y);
  y += 20;
  
  drawWrappedText(UI_10_FONT_ID, margin, y, message, width - 2 * margin);
  
  GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// ---------------------------------------------------------------------------
// Seek past the file header to the first '[' of the "questions" array.
// Returns true on success, false if the array marker was not found.
// ---------------------------------------------------------------------------
static bool seekToQuestionsArray(HalFile& file) {
  char searchBuf[32] = {0};
  int searchPos = 0;

  while (file.available()) {
    char c = file.read();

    if (searchPos < 31) {
      searchBuf[searchPos++] = c;
    } else {
      memmove(searchBuf, searchBuf + 1, 30);
      searchBuf[30] = c;
    }

    if (strstr(searchBuf, "\"questions\"") != NULL) {
      while (file.available()) {
        c = file.read();
        if (c == '[') return true;
        if (c == '{' || c == '}') return false;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// countQuestionsInFile()
// Fast: reads the "total_questions": N field from the file header (first
// ~256 bytes). Returns 0 if not found.
// ---------------------------------------------------------------------------
int AWSCertQuizActivity::countQuestionsInFile() {
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());
  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) return 0;

  char header[256] = {0};
  int bytesRead = file.read(header, sizeof(header) - 1);
  file.close();
  if (bytesRead <= 0) return 0;
  header[bytesRead] = '\0';

  const char* tag = strstr(header, "\"total_questions\"");
  if (!tag) return 0;
  const char* colon = strchr(tag, ':');
  if (!colon) return 0;
  return atoi(colon + 1);
}

// ---------------------------------------------------------------------------
// randomSelectIndices()
// Picks `needed` unique random indices from [0, total-1], returned sorted.
// ---------------------------------------------------------------------------
void AWSCertQuizActivity::randomSelectIndices(int total, int needed, std::vector<uint16_t>& outIndices) {
  outIndices.clear();
  if (total <= 0) return;
  if (needed >= total) {
    outIndices.resize(total);
    for (int i = 0; i < total; i++) outIndices[i] = (uint16_t)i;
    return;
  }
  // Fisher-Yates on a pool of [0..total-1], take first `needed`.
  // Pool fits on stack for files up to ~300 questions (600 bytes).
  std::vector<uint16_t> pool(total);
  for (int i = 0; i < total; i++) pool[i] = (uint16_t)i;

  std::seed_seq seed{esp_random(), esp_random(), esp_random()};
  std::mt19937 g(seed);
  std::shuffle(pool.begin(), pool.end(), g);

  outIndices.assign(pool.begin(), pool.begin() + needed);
  std::sort(outIndices.begin(), outIndices.end());  // sorted for sequential file access
}

// ---------------------------------------------------------------------------
// weightedSelectIndices()
// Spaced-repetition variant of randomSelectIndices: previously-missed
// questions (from the v3 history) get weight 1 + 2×missCount so they are
// several times more likely to be drawn, without ever excluding the rest of
// the bank. mapToFileIndex translates a pool position to the fileIndex the
// history is keyed by (identity for whole-bank selection; domainIndices[i]
// for domain mode). Output contract matches randomSelectIndices: unique,
// sorted ascending.
// ---------------------------------------------------------------------------
void AWSCertQuizActivity::weightedSelectIndices(int total, int needed, std::vector<uint16_t>& outIndices,
                                                const uint16_t* poolToFileIndex) {
  outIndices.clear();
  if (total <= 0) return;
  if (needed >= total) {
    outIndices.resize(total);
    for (int i = 0; i < total; i++) outIndices[i] = (uint16_t)i;
    return;
  }

  std::vector<HistoryRecord> records;
  if (!loadHistoryRecords(records)) {
    // No history — plain uniform selection
    randomSelectIndices(total, needed, outIndices);
    return;
  }

  // Build per-pool-position weights
  std::vector<uint8_t> weights(total, 1);
  uint32_t totalWeight = (uint32_t)total;
  for (int i = 0; i < total; i++) {
    const uint16_t fi = poolToFileIndex ? poolToFileIndex[i] : (uint16_t)i;
    for (const auto& rec : records) {
      if (rec.fileIndex == fi) {
        const uint8_t extra = (uint8_t)std::min<int>(2 * rec.missCount, 250);
        weights[i] += extra;
        totalWeight += extra;
        break;
      }
    }
  }

  // Weighted sampling without replacement: draw, zero the winner's weight
  std::seed_seq seed{esp_random(), esp_random(), esp_random()};
  std::mt19937 g(seed);
  outIndices.reserve(needed);
  for (int drawn = 0; drawn < needed && totalWeight > 0; drawn++) {
    std::uniform_int_distribution<uint32_t> dist(0, totalWeight - 1);
    uint32_t r = dist(g);
    for (int i = 0; i < total; i++) {
      if (weights[i] == 0) continue;
      if (r < weights[i]) {
        outIndices.push_back((uint16_t)i);
        totalWeight -= weights[i];
        weights[i] = 0;
        break;
      }
      r -= weights[i];
    }
  }
  std::sort(outIndices.begin(), outIndices.end());  // sorted for sequential file access

  DEBUG_PRINTF("[AWS] Weighted selection: %d of %d (history records: %d)\n", (int)outIndices.size(), total,
               (int)records.size());
}

// ---------------------------------------------------------------------------
// cheapDomainScan()
// Scans the file and returns the sequential file indices of questions whose
// "domain" field matches `domain` (case-insensitive). Uses the same
// brace-depth buffer as loadCustomQuestions but skips full JSON parsing.
// If domain is "all", returns indices for every question.
// ---------------------------------------------------------------------------
bool AWSCertQuizActivity::cheapDomainScan(const char* domain, std::vector<uint16_t>& outIndices) {
  outIndices.clear();
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());

  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) return false;
  if (!seekToQuestionsArray(file)) { file.close(); return false; }

  const bool matchAll = (strcmp(domain, "all") == 0);
  char* buf = (char*)malloc(JSON_PARSE_BUF_SIZE);
  if (!buf) { file.close(); return false; }

  int seqIndex = 0;
  int braceDepth = 0;
  int bufPos = 0;

  while (file.available()) {
    char c = file.read();

    if (c == '{') {
      if (braceDepth == 0) bufPos = 0;
      braceDepth++;
    }
    if (braceDepth > 0 && bufPos < (int)JSON_PARSE_BUF_SIZE - 1) {
      buf[bufPos++] = c;
    }
    if (c == '}') {
      braceDepth--;
      if (braceDepth == 0 && bufPos > 10) {
        buf[bufPos] = '\0';
        bool matches = matchAll;
        if (!matches) {
          const char* domainStart = strstr(buf, "\"domain\":");
          if (domainStart) {
            const char* valStart = strchr(domainStart + 9, '"');
            if (valStart) {
              valStart++;
              const char* valEnd = strchr(valStart, '"');
              if (valEnd && (valEnd - valStart) < 63) {
                char domainVal[64];
                int len = valEnd - valStart;
                memcpy(domainVal, valStart, len);
                domainVal[len] = '\0';
                matches = (strcasecmp(domainVal, domain) == 0);
              }
            }
          }
        }
        if (matches) outIndices.push_back((uint16_t)seqIndex);
        seqIndex++;
      }
    }
    if (c == ']' && braceDepth == 0) break;
  }

  free(buf);
  file.close();
  return true;
}

// ---------------------------------------------------------------------------
// peekSessionFileIndices()
// Opens the session file for the current cert/mode/domain and reads just the
// file-index array (the third section of the v2 session format). Returns true
// and populates outIndices if a valid v2 session exists.
// ---------------------------------------------------------------------------
bool AWSCertQuizActivity::peekSessionFileIndices(std::vector<uint16_t>& outIndices) {
  outIndices.clear();
  if (practiceMode == "review" || practiceMode == "quickstart") return false;

  char safeDomain[48];
  sanitizeFilenameComponent(practiceDomain.c_str(), safeDomain, sizeof(safeDomain));
  char path[256];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s-%s-%s.dat",
           certId.c_str(), practiceMode.c_str(), safeDomain);

  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) return false;

  int savedCurrentIndex, savedQuestionCount;
  if (file.read((uint8_t*)&savedCurrentIndex, sizeof(savedCurrentIndex)) != sizeof(savedCurrentIndex) ||
      file.read((uint8_t*)&savedQuestionCount, sizeof(savedQuestionCount)) != sizeof(savedQuestionCount)) {
    file.close(); return false;
  }
  if (savedQuestionCount <= 0 || savedQuestionCount > MAX_QUESTIONS_FROM_FILE) {
    file.close(); return false;
  }

  // v2 session: next section is fileIndices (uint16_t × N)
  // Verify there are enough bytes remaining for fileIndices + questionOrder + userAnswers
  size_t expectedRemaining = (size_t)savedQuestionCount * (sizeof(uint16_t) + sizeof(uint16_t) + sizeof(int8_t));
  if (file.size() < (size_t)(8 + expectedRemaining)) {
    // Old format (no fileIndices section) — discard
    file.close(); return false;
  }

  outIndices.clear();
  outIndices.reserve(savedQuestionCount);
  for (int i = 0; i < savedQuestionCount; i++) {
    uint16_t val;
    if (file.read((uint8_t*)&val, sizeof(val)) != sizeof(val)) {
      file.close(); outIndices.clear(); return false;
    }
    outIndices.push_back(val);
  }

  file.close();
  return true;
}

// ---------------------------------------------------------------------------
// loadCustomQuestions()
// Parses the JSON file one question at a time.
// If onlyFileIndices is provided (sorted), only questions at those sequential
// positions are stored — all others are skipped cheaply (brace-counted but
// not deserialized). If nullptr, loads all questions up to
// MAX_QUESTIONS_FROM_FILE with a heap guard to prevent OOM crashes.
// ---------------------------------------------------------------------------
bool AWSCertQuizActivity::loadCustomQuestions(const std::vector<uint16_t>* onlyFileIndices) {
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());
  DEBUG_PRINTF("[AWS] Trying to load: %s\n", path);

  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTF("[AWS] Failed to open file: %s\n", path);
    return false;
  }

  DEBUG_PRINTF("[AWS] File opened, size: %d bytes\n", file.size());
  customQuestions.clear();

  if (!seekToQuestionsArray(file)) {
    DEBUG_PRINTLN("[AWS] Could not find questions array");
    file.close();
    return false;
  }
  DEBUG_PRINTLN("[AWS] Found questions array");

  // Pre-size the vector if we know exactly how many questions we'll load
  if (onlyFileIndices && !onlyFileIndices->empty()) {
    customQuestions.reserve(onlyFileIndices->size());
  }

  // Index into onlyFileIndices for efficient forward scanning
  int targetCursor = 0;
  const int targetCount = onlyFileIndices ? (int)onlyFileIndices->size() : INT_MAX;

  int parsedCount = 0;
  int seqIndex = 0;       // sequential question index in the file (0-based)
  int braceDepth = 0;

  char* buffer = (char*)malloc(JSON_PARSE_BUF_SIZE);
  if (!buffer) {
    DEBUG_PRINTLN("[AWS] Failed to allocate parse buffer");
    file.close();
    return false;
  }
  int bufferPos = 0;

  DEBUG_PRINTF("[AWS] Free memory before parsing: %d bytes\n", ESP.getFreeHeap());

  while (file.available() && targetCursor < targetCount) {
    // Heap guard on BOTH paths: a 75-question exam of long questions can
    // need >100KB of string heap, and std::string growth aborts on OOM
    // under -fno-exceptions. Stop loading and run with what we have.
    if (parsedCount > 0 && parsedCount % 10 == 0 && ESP.getFreeHeap() < 60000) {
      DEBUG_PRINTF("[AWS] Heap guard triggered after %d questions (%d bytes free)\n",
                   parsedCount, ESP.getFreeHeap());
      break;
    }
    // Question-count cap for the unfiltered fallback path
    if (!onlyFileIndices && parsedCount >= MAX_QUESTIONS_FROM_FILE) break;

    char c = file.read();

    if (c == '{') {
      if (braceDepth == 0) bufferPos = 0;
      braceDepth++;
    }

    // Determine whether we want to capture this question's content
    bool capture = true;
    if (onlyFileIndices && braceDepth >= 1) {
      // Only buffer if this seqIndex is in our target list
      capture = (targetCursor < targetCount &&
                 (*onlyFileIndices)[targetCursor] == (uint16_t)seqIndex);
    }

    if (braceDepth > 0 && capture && bufferPos < (int)JSON_PARSE_BUF_SIZE - 1) {
      buffer[bufferPos++] = c;
    }

    if (c == '}') {
      braceDepth--;
      if (braceDepth == 0 && bufferPos > 10) {
        buffer[bufferPos] = '\0';

        // Only deserialize if this seqIndex was a target (or we're in fallback mode)
        bool shouldDeserialize = !onlyFileIndices ||
                                 (targetCursor < targetCount &&
                                  (*onlyFileIndices)[targetCursor] == (uint16_t)seqIndex);

        if (shouldDeserialize) {
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, buffer);

          if (!error) {
            JsonObject q = doc.as<JsonObject>();
            bool questionValid = q["question"].is<const char*>() && !q["options"].isNull();
            if (questionValid) {
              const char* optA = nullptr; const char* optB = nullptr;
              const char* optC = nullptr; const char* optD = nullptr;

              if (q["options"].is<JsonArray>() && q["options"].size() >= 4 &&
                  q["options"][0].is<const char*>() && q["options"][1].is<const char*>() &&
                  q["options"][2].is<const char*>() && q["options"][3].is<const char*>()) {
                optA = q["options"][0].as<const char*>();
                optB = q["options"][1].as<const char*>();
                optC = q["options"][2].as<const char*>();
                optD = q["options"][3].as<const char*>();
              } else if (q["options"].is<JsonObject>()) {
                JsonObject opts = q["options"].as<JsonObject>();
                if (opts["A"].is<const char*>() && opts["B"].is<const char*>() &&
                    opts["C"].is<const char*>() && opts["D"].is<const char*>()) {
                  optA = opts["A"].as<const char*>();
                  optB = opts["B"].as<const char*>();
                  optC = opts["C"].as<const char*>();
                  optD = opts["D"].as<const char*>();
                }
              }

              if (optA && optB && optC && optD) {
                uint8_t correctIdx = 0;
                JsonVariant correctField = q["correct"];
                if (correctField.isNull()) correctField = q["correct_answer"];
                if (correctField.is<int>()) {
                  correctIdx = std::min(static_cast<uint8_t>(correctField.as<int>()), static_cast<uint8_t>(3));
                } else if (correctField.is<const char*>()) {
                  const char* letter = correctField.as<const char*>();
                  if (letter && letter[0] >= 'A' && letter[0] <= 'D')
                    correctIdx = static_cast<uint8_t>(letter[0] - 'A');
                }

                Question question;
                question.certId = certId.c_str();
                question.fileIndex = (uint16_t)seqIndex;
                question.question = q["question"].as<const char*>();
                question.options[0] = optA;
                question.options[1] = optB;
                question.options[2] = optC;
                question.options[3] = optD;
                question.correct = correctIdx;
                question.explanation = q["explanation"] | "No explanation available";
                question.domain = q["domain"] | "General";
                if (question.domain.empty()) question.domain = "General";
                question.difficulty = q["difficulty"] | "medium";
                customQuestions.push_back(std::move(question));
                parsedCount++;
              } else {
                DEBUG_PRINTF("[AWS] Skipped invalid question at seq %d\n", seqIndex);
              }
            }
          } else {
            DEBUG_PRINTF("[AWS] JSON parse error at seq %d: %s\n", seqIndex, error.c_str());
          }
          targetCursor++;  // Advance whether parse succeeded or not
        }

        seqIndex++;
        bufferPos = 0;  // Reset for next question

        if (parsedCount % 10 == 0) {
          DEBUG_PRINTF("[AWS] Free memory after %d questions: %d bytes\n", parsedCount, ESP.getFreeHeap());
        }
      } else if (braceDepth == 0) {
        // Question block completed without buffer content (was skipped) — advance seqIndex
        seqIndex++;
        bufferPos = 0;
      }
    }

    if (c == ']' && braceDepth == 0) break;
  }

  free(buffer);
  file.close();

  DEBUG_PRINTF("[AWS] Loaded %d questions, free memory: %d bytes\n", customQuestions.size(), ESP.getFreeHeap());
  return customQuestions.size() > 0;
}


void AWSCertQuizActivity::renderQuestion() {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();

  if (currentIndex < 0 || currentIndex >= (int)questionOrder.size()) return;
  int actualIndex = questionOrder[currentIndex];
  if (actualIndex < 0 || actualIndex >= (int)customQuestions.size()) return;
  const Question* q = &customQuestions[actualIndex];
  
  // ── Header ────────────────────────────────────────────────────────────────
  // Row 1 (y=8):  progress bar
  int progressBarWidth = width - 2 * margin;
  int progressFilled = questionCount > 0 ? (progressBarWidth * (currentIndex + 1)) / questionCount : 0;
  renderer.drawRect(margin, 8, progressBarWidth, 6);
  renderer.fillRect(margin, 8, progressFilled, 6);

  // Row 2 (y=20):  left = "Q 5/65" or "Q 5/65  3 ans"
  //                right = "medium  03:45" (difficulty + optional timer/streak)
  //                Right-aligned so it can never collide with the left text.
  {
    int answeredSoFar = 0;
    for (int i = 0; i < questionCount && i < (int)userAnswers.size(); i++) {
      if (userAnswers[i] >= 0) answeredSoFar++;
    }

    // Left side — include domain name when in domain mode so user knows which domain they're on
    char leftText[96];
    if (practiceMode == "domain" && practiceDomain != "all" && !practiceDomain.isEmpty()) {
      snprintf(leftText, sizeof(leftText), "Q %d/%d  %d ans  [%.28s]",
               currentIndex + 1, questionCount, answeredSoFar, practiceDomain.c_str());
    } else {
      snprintf(leftText, sizeof(leftText), "Q %d/%d  %d ans", currentIndex + 1, questionCount, answeredSoFar);
    }
    renderer.drawText(UI_10_FONT_ID, margin, 20, leftText, true);

    // Right side: build a single string "difficulty  MM:SS" or just "difficulty"
    // then draw it right-aligned by measuring its width.
    char rightText[32];
    if (showTimer) {
      unsigned long elapsed = (millis() - startTime) / 1000;
      snprintf(rightText, sizeof(rightText), "%s  %02d:%02d",
               q->difficulty.c_str(), (int)(elapsed / 60), (int)(elapsed % 60));
    } else if (currentStreak >= 3) {
      snprintf(rightText, sizeof(rightText), "%s  %d correct!", q->difficulty.c_str(), currentStreak);
    } else {
      snprintf(rightText, sizeof(rightText), "%s", q->difficulty.c_str());
    }
    int rw = renderer.getTextWidth(UI_10_FONT_ID, rightText);
    if (rw <= 0) rw = (int)strlen(rightText) * 7;  // Fallback: ~7px per char at UI_10
    renderer.drawText(UI_10_FONT_ID, width - margin - rw, 20, rightText, true);
  }

  // Separator — sits 6px below text bottom (text y=20, ~14px tall → bottom ≈36, line at 42)
  renderer.drawLine(margin, 48, width - margin, 48);

  // ── Question text ─────────────────────────────────────────────────────────
  int y = 58;
  int textHeight = drawWrappedText(UI_10_FONT_ID, margin, y, q->question.c_str(), width - 2 * margin);
  y += textHeight + 10;
  
  // Options — "A:  " / "B:  " labels with clear visual separation from answer text
  static const char* optLabels[]      = { "A", "B", "C", "D" };
  static const char* optLabelColons[] = { "A:", "B:", "C:", "D:" };
  const int labelX   = margin + 4;   // Left edge of "A:" label
  const int textX    = margin + 34;  // Left edge of option text (after "A:  " gap)
  const int textW    = width - textX - margin;

  for (int i = 0; i < 4; i++) {
    int optionStartY = y;
    int optionHeight = drawWrappedText(UI_10_FONT_ID, textX, y + 6, q->options[i].c_str(), textW, true);
    if (optionHeight < MIN_OPTION_HEIGHT) optionHeight = MIN_OPTION_HEIGHT;

    if (i == selectedOption) {
      // Filled (inverted) background = user's current selection
      renderer.fillRect(margin, optionStartY - 2, width - 2 * margin, optionHeight + 12);
      renderer.drawText(UI_10_FONT_ID, labelX, optionStartY + 6, optLabelColons[i], false);
      drawWrappedText(UI_10_FONT_ID, textX, optionStartY + 6, q->options[i].c_str(), textW, false);
    } else {
      renderer.drawText(UI_10_FONT_ID, labelX, optionStartY + 6, optLabelColons[i], true);
    }

    y += optionHeight + 16;
  }

  // "Answer: X" — shows only after explicit Select press; guard against off-screen rendering
  {
    const int height = renderer.getScreenHeight();
    if (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0
        && userAnswers[currentIndex] < 4
        && y + 6 < height - 50) {
      char selText[20];
      snprintf(selText, sizeof(selText), fork_tr(STR_AWS_ANSWER_FMT), optLabels[userAnswers[currentIndex]]);
      renderer.drawText(UI_10_FONT_ID, margin, y + 6, selText, true);
    }
  }
  
  // Button hints (match physical button layout)
  const char* btn1 = tr(STR_BACK);
  const char* btn2;
  const char* btn3 = currentIndex > 0 ? fork_tr(STR_BTN_PREV) : "";
  const char* btn4;
  
  if (practiceMode == "study") {
    // Study mode: "Answer" only shown once user has highlighted an option
    btn2 = selectedOption >= 0 ? fork_tr(STR_BTN_ANSWER) : "";
    btn4 = currentIndex < questionCount - 1 ? tr(STR_NEXT_FIELD) : fork_tr(STR_BTN_FINISH);
  } else {
    // Exam mode: Up/Down to highlight, Select to confirm, Next/Prev to navigate freely
    btn2 = selectedOption >= 0 ? tr(STR_SELECT) : "";
    btn4 = currentIndex < questionCount - 1 ? tr(STR_NEXT_FIELD) : fork_tr(STR_BTN_SUBMIT);
  }
  
  GUI.drawButtonHints(renderer, btn1, btn2, btn3, btn4);
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::renderAnswer() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();

  if (currentIndex < 0 || currentIndex >= (int)questionOrder.size()) return;
  int actualIndex = questionOrder[currentIndex];
  if (actualIndex < 0 || actualIndex >= (int)customQuestions.size()) return;
  const Question* q = &customQuestions[actualIndex];
  // Use the saved answer (userAnswers), not the cursor position (selectedOption)
  int savedAnswer = (currentIndex < (int)userAnswers.size()) ? userAnswers[currentIndex] : -1;
  bool correct = (savedAnswer >= 0) && (savedAnswer == q->correct);
  
  // Result with icon
  int iconX = margin;
  int iconY = margin;
  if (correct) {
    drawCheckmark(iconX, iconY, 30);
    renderer.drawText(UI_12_FONT_ID, iconX + 40, iconY + 5, fork_tr(STR_AWS_ANSWER_CORRECT), true);

    // Encouragement for streaks
    if (currentStreak == 3) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, fork_tr(STR_AWS_ANSWER_ON_FIRE), true);
    } else if (currentStreak == 5) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, fork_tr(STR_AWS_ANSWER_UNSTOPPABLE), true);
    } else if (currentStreak >= 7) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, fork_tr(STR_AWS_ANSWER_PERFECT), true);
    }
  } else {
    drawXMark(iconX, iconY, 30);
    renderer.drawText(UI_12_FONT_ID, iconX + 40, iconY + 5, fork_tr(STR_AWS_ANSWER_INCORRECT), true);
    renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, fork_tr(STR_AWS_ANSWER_KEEP_LEARNING), true);
  }
  
  // Progress bar
  int progressBarWidth = width - 2 * margin;
  int progressFilled = (progressBarWidth * (currentIndex + 1)) / questionCount;
  renderer.drawRect(margin, iconY + 40, progressBarWidth, 8);
  renderer.fillRect(margin, iconY + 40, progressFilled, 8);
  
  // Correct answer
  int y = iconY + 60;
  char correctText[128];
  snprintf(correctText, sizeof(correctText), "Answer: %s", q->options[q->correct].c_str());
  int answerHeight = drawWrappedText(UI_10_FONT_ID, margin, y, correctText, width - 2 * margin);
  y += answerHeight + 10;

  // Explanation (wrapped)
  int explainHeight = drawWrappedText(UI_10_FONT_ID, margin, y, q->explanation.c_str(), width - 2 * margin);
  y += explainHeight + 20;
  
  // Score with visual indicator — compute from userAnswers so it's accurate mid-quiz
  int runningScore = 0;
  int answeredCount = 0;
  for (int i = 0; i <= currentIndex && i < (int)userAnswers.size(); i++) {
    if (userAnswers[i] >= 0) {
      answeredCount++;
      int aIdx = questionOrder[i];
      if (aIdx < (int)customQuestions.size() && userAnswers[i] == customQuestions[aIdx].correct) {
        runningScore++;
      }
    }
  }
  char scoreText[64];
  snprintf(scoreText, sizeof(scoreText), fork_tr(STR_AWS_SCORE_FMT), runningScore, answeredCount);
  renderer.drawText(UI_10_FONT_ID, margin, y, scoreText, true);

  // Score bar
  int scoreBarWidth = 150;
  int scoreFilled = answeredCount > 0 ? (scoreBarWidth * runningScore) / answeredCount : 0;
  renderer.drawRect(margin + 100, y + 5, scoreBarWidth, 10);
  renderer.fillRect(margin + 100, y + 5, scoreFilled, 10);
  
  // Button hints: Confirm re-shows the current question; Right advances (or finishes)
  const char* btn1 = tr(STR_BACK);
  const char* btn2 = fork_tr(STR_BTN_REVIEW);
  const char* btn3 = currentIndex > 0 ? fork_tr(STR_BTN_PREV) : "";
  const char* btn4 = currentIndex < questionCount - 1 ? tr(STR_NEXT_FIELD) : tr(STR_DONE);
  GUI.drawButtonHints(renderer, btn1, btn2, btn3, btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::renderSummary() const {
  if (questionCount == 0) return;  // Guard against division by zero
  renderer.clearScreen();

  // RAII guard: ensures displayBuffer is called on every return path
  struct DisplayFlusher {
    GfxRenderer& r;
    explicit DisplayFlusher(GfxRenderer& renderer) : r(renderer) {}
    ~DisplayFlusher() { r.displayBuffer(HalDisplay::FAST_REFRESH); }
  } flusher{renderer};

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();  // used for button positioning

  // Count answered questions
  int totalAnswered = 0;
  for (int i = 0; i < questionCount && i < (int)userAnswers.size(); i++) {
    if (userAnswers[i] >= 0) totalAnswered++;
  }

  // Full exam uses total questions as denominator (skipping = wrong, like the real AWS exam).
  // Practice/study/domain modes use answered-only so accuracy reflects what was attempted.
  int percentage;
  if (practiceMode == "full") {
    percentage = (correctCount * 100) / questionCount;
  } else {
    percentage = totalAnswered > 0 ? (correctCount * 100) / totalAnswered : 0;
  }
  int passingScore = getPassingScoreForCert();
  bool passed = (totalAnswered > 0) && (percentage >= passingScore);

  // AWS Logo
  drawAWSLogo(width / 2 - 20, margin);

  // Title
  renderer.drawText(UI_12_FONT_ID, margin, margin + 50, fork_tr(STR_AWS_QUIZ_COMPLETE), true);

  // Pass/Fail Badge — bail out with a message if nothing was answered
  int badgeY = margin + 90;
  int badgeX = width / 2 - 60;
  if (totalAnswered == 0) {
    renderer.drawText(UI_12_FONT_ID, margin, badgeY + 50, fork_tr(STR_AWS_NO_ANSWERS), true);
    renderer.drawText(UI_10_FONT_ID, margin, badgeY + 80, fork_tr(STR_AWS_SELECT_SUBMIT), true);
    GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
    return;
  } else if (passed) {
    drawPassBadge(badgeX, badgeY);
  } else {
    drawFailBadge(badgeX, badgeY);
  }

  // Score
  int y = badgeY + 140;
  char scoreText[64];
  if (totalAnswered < questionCount) {
    snprintf(scoreText, sizeof(scoreText), fork_tr(STR_AWS_SCORE_SKIPPED_FMT),
             correctCount, totalAnswered, questionCount - totalAnswered);
  } else {
    snprintf(scoreText, sizeof(scoreText), fork_tr(STR_AWS_SCORE_FMT), correctCount, questionCount);
  }
  renderer.drawText(UI_12_FONT_ID, margin, y, scoreText, true);
  y += 40;

  char percentText[32];
  snprintf(percentText, sizeof(percentText), "(%d%%)", percentage);
  renderer.drawText(UI_10_FONT_ID, margin, y, percentText, true);
  y += 30;

  // Elapsed time (full exam only)
  if (showTimer && startTime > 0) {
    unsigned long elapsed = ((endTime > 0 ? endTime : millis()) - startTime) / 1000;
    int minutes = elapsed / 60;
    int seconds = elapsed % 60;
    char timeText[32];
    snprintf(timeText, sizeof(timeText), "Time: %02d:%02d", minutes, seconds);
    renderer.drawText(UI_10_FONT_ID, margin, y, timeText, true);
    y += 30;
  } else {
    y += 10;
  }

  // Score visualization
  int barWidth = width - 2 * margin;
  int barHeight = 30;
  renderer.drawRect(margin, y, barWidth, barHeight);

  // Fill based on percentage
  int fillWidth = (barWidth * percentage) / 100;
  renderer.fillRect(margin, y, fillWidth, barHeight);

  // Passing score threshold marker
  char passLabel[8];
  snprintf(passLabel, sizeof(passLabel), "%d%%", passingScore);
  renderer.drawLine(margin + (barWidth * passingScore) / 100, y - 5, margin + (barWidth * passingScore) / 100, y + barHeight + 5);
  renderer.drawText(UI_10_FONT_ID, margin + (barWidth * passingScore) / 100 - 10, y + barHeight + 15, passLabel, true);
  
  y += 60;
  
  // Streak achievement
  if (longestStreak >= 3) {
    char streakText[64];
    snprintf(streakText, sizeof(streakText), "Best run: %d correct in a row!", longestStreak);
    renderer.drawText(UI_10_FONT_ID, margin, y, streakText, true);
    
    // Draw fire emoji representation (simple)
    if (longestStreak >= 5) {
      renderer.drawText(UI_12_FONT_ID, margin + 200, y - 5, "***", true);
    }
    y += 30;
  }
  
  // Review incorrect option
  if (incorrectQuestions.size() > 0) {
    char reviewText[64];
    snprintf(reviewText, sizeof(reviewText), fork_tr(STR_AWS_REVIEW_MISTAKES_FMT), incorrectQuestions.size());
    renderer.drawRect(margin, y, 200, 35);
    renderer.drawText(UI_10_FONT_ID, margin + 10, y + 10, reviewText, true);
    y += 45;
  }
  
  // Domain stats option
  renderer.drawRect(margin, y, 200, 35);
  renderer.drawText(UI_10_FONT_ID, margin + 10, y + 10, fork_tr(STR_AWS_DOMAIN_BREAKDOWN), true);
  
  const char* legend = incorrectQuestions.size() > 0 ?
                       "Back | Confirm: Review | Down: Domains" : "Back | Down: Domains";
  renderer.drawText(UI_10_FONT_ID, margin, height - 40, legend, true);
}

void AWSCertQuizActivity::renderReview() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();

  if (reviewIndex < 0 || reviewIndex >= (int)incorrectQuestions.size()) return;
  int questionIdx = incorrectQuestions[reviewIndex];
  if (questionIdx < 0 || questionIdx >= (int)questionOrder.size()) return;
  if (questionIdx >= (int)userAnswers.size()) return;
  int actualIndex = questionOrder[questionIdx];
  if (actualIndex < 0 || actualIndex >= (int)customQuestions.size()) return;
  const Question* q = &customQuestions[actualIndex];
  int userAnswer = userAnswers[questionIdx];
  
  // Header
  int y = margin;
  char header[64];
  snprintf(header, sizeof(header), fork_tr(STR_AWS_REVIEW_FMT), reviewIndex + 1, (int)incorrectQuestions.size());
  renderer.drawText(UI_12_FONT_ID, margin, y, header, true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  
  renderer.drawLine(margin, y, width - margin, y);
  y += 20;
  
  // Question
  y += drawWrappedText(UI_10_FONT_ID, margin, y, q->question.c_str(), width - 2 * margin);
  y += 20;

  // Show all options with indicators
  for (int i = 0; i < 4; i++) {
    int optionY = y;

    // Indicator
    if (i == q->correct) {
      drawCheckmark(margin, optionY, 15);
      renderer.drawText(UI_10_FONT_ID, margin + 20, optionY, fork_tr(STR_AWS_CORRECT), true);
    } else if (userAnswer >= 0 && i == userAnswer) {
      drawXMark(margin, optionY, 15);
      renderer.drawText(UI_10_FONT_ID, margin + 20, optionY, fork_tr(STR_AWS_YOUR_ANSWER), true);
    }

    y += 20;
    y += drawWrappedText(UI_10_FONT_ID, margin + 20, y, q->options[i].c_str(), width - 2 * margin - 20);
    y += 10;
  }

  y += 10;

  // Explanation
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_EXPLANATION), true);
  y += 20;
  y += drawWrappedText(UI_10_FONT_ID, margin, y, q->explanation.c_str(), width - 2 * margin);
  
  // btn2=Confirm(Next/Done), btn3=Left(Prev), btn4=Right also works but not labelled
  const char* btn2 = reviewIndex < (int)incorrectQuestions.size() - 1 ? tr(STR_NEXT_FIELD) : tr(STR_DONE);
  const char* btn3 = reviewIndex > 0 ? fork_tr(STR_BTN_PREV) : "";
  const char* btn4 = "";
  GUI.drawButtonHints(renderer, tr(STR_BACK), btn2, btn3, btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (state == REVIEW || state == DOMAIN_STATS) {
      state = SUMMARY;
      renderSummary();
    } else {
      onBack();
    }
    return;
  } else if (state == LOADING) {
    // Waiting for user to press back (error state)
    return;
  } else if (state == QUESTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedOption = (selectedOption < 0) ? 3 : (selectedOption + 3) % 4;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedOption = (selectedOption < 0) ? 0 : (selectedOption + 1) % 4;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      // Navigate to previous question — no session write: the session is saved
      // on every answer confirm, so a crash here only loses the cursor position
      if (currentIndex > 0) {
        currentIndex--;
        // Restore cursor to saved answer for that question (or -1 if unanswered)
        selectedOption = (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0)
                         ? userAnswers[currentIndex] : -1;
        renderQuestion();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Navigate to next question — no session write (see Left handler above)
      if (currentIndex < questionCount - 1) {
        currentIndex++;
        // Restore cursor to saved answer for the next question (or -1 if unanswered)
        selectedOption = (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0)
                         ? userAnswers[currentIndex] : -1;
        renderQuestion();
      } else {
        // Last question - calculate results
        correctCount = 0;
        incorrectQuestions.clear();
        domainCorrect.clear();
        domainTotal.clear();
        currentStreak = 0;
        longestStreak = 0;

        for (int i = 0; i < questionCount; i++) {
          if (userAnswers[i] < 0) continue;  // Skip unanswered

          int actualIndex = questionOrder[i];
          const Question* q = &customQuestions[actualIndex];

          // Populate domain maps (only for answered questions)
          String domainKey(q->domain.c_str());
          domainTotal[domainKey]++;

          if (userAnswers[i] == q->correct) {
            correctCount++;
            currentStreak++;
            if (currentStreak > longestStreak) longestStreak = currentStreak;
            domainCorrect[domainKey]++;
          } else {
            currentStreak = 0;
            incorrectQuestions.push_back(i);
          }
        }
        
        clearSession();  // Quiz complete - clear session
        saveIncorrectHistory();  // Save incorrect questions for review mode
        saveQuizStats();  // Save stats for tracking
        if (showTimer) endTime = millis();
        state = SUMMARY;
        renderSummary();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (practiceMode == "study" && selectedOption >= 0) {
        // Study mode: show answer — only if user has actually highlighted an option
        userAnswers[currentIndex] = selectedOption;
        saveSession();
        state = ANSWER;
        renderAnswer();
      } else if (selectedOption >= 0) {
        // Exam mode: explicitly select/confirm the highlighted option as the answer
        userAnswers[currentIndex] = selectedOption;
        saveSession();
        renderQuestion();  // Redraw to show "Answer: X"
      }
    }
  } else if (state == ANSWER) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Return to question view — restore cursor to saved answer for consistency
      selectedOption = (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0)
                       ? userAnswers[currentIndex] : -1;
      state = QUESTION;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      // Previous question — restore cursor for destination question
      if (currentIndex > 0) {
        currentIndex--;
        selectedOption = (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0)
                         ? userAnswers[currentIndex] : -1;
        state = QUESTION;
        renderQuestion();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Next question — restore cursor for destination question
      if (currentIndex < questionCount - 1) {
        currentIndex++;
        selectedOption = (currentIndex < (int)userAnswers.size() && userAnswers[currentIndex] >= 0)
                         ? userAnswers[currentIndex] : -1;
        state = QUESTION;
        renderQuestion();
      } else {
        // Last question in study mode - calculate results before showing summary
        correctCount = 0;
        incorrectQuestions.clear();
        domainCorrect.clear();
        domainTotal.clear();
        currentStreak = 0;
        longestStreak = 0;

        for (int i = 0; i < questionCount; i++) {
          if (userAnswers[i] < 0) continue;  // Skip unanswered

          int actualIndex = questionOrder[i];
          const Question* q = &customQuestions[actualIndex];

          // Populate domain maps (only for answered questions)
          String domainKey(q->domain.c_str());
          domainTotal[domainKey]++;

          if (userAnswers[i] == q->correct) {
            correctCount++;
            currentStreak++;
            if (currentStreak > longestStreak) longestStreak = currentStreak;
            domainCorrect[domainKey]++;
          } else {
            currentStreak = 0;
            incorrectQuestions.push_back(i);
          }
        }

        clearSession();  // Quiz complete - clear session
        saveIncorrectHistory();  // Save incorrect questions for review mode
        saveQuizStats();  // Save stats for tracking
        if (showTimer) endTime = millis();
        state = SUMMARY;
        renderSummary();
      }
    }
  } else if (state == SUMMARY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Start reviewing incorrect questions
      if (incorrectQuestions.size() > 0) {
        reviewIndex = 0;
        state = REVIEW;
        renderReview();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      // Show domain stats
      state = DOMAIN_STATS;
      renderDomainStats();
    }
  } else if (state == DOMAIN_STATS) {
    // Back is handled by the global handler above; Confirm/Right also return to summary
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      state = SUMMARY;
      renderSummary();
    }
  } else if (state == REVIEW) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (reviewIndex > 0) {
        reviewIndex--;
        renderReview();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      reviewIndex++;
      if (reviewIndex < (int)incorrectQuestions.size()) {
        renderReview();
      } else {
        state = SUMMARY;
        renderSummary();
      }
    }
  }
}

// UI Helper Functions
void AWSCertQuizActivity::drawAWSLogo(int x, int y) const {
  // Simple AWS smile logo
  int size = 35;
  
  // Smile curve
  for (int i = 0; i < size; i++) {
    int offset = (i * i) / (size * 2);
    renderer.drawLine(x + i, y + 15 + offset, x + i, y + 17 + offset);
  }
  
  // Arrow tip
  renderer.drawLine(x + size - 5, y + 20, x + size, y + 25);
  renderer.drawLine(x + size - 5, y + 30, x + size, y + 25);
}

void AWSCertQuizActivity::drawCheckmark(int x, int y, int size) const {
  // Draw checkmark
  int midX = x + size / 3;
  int midY = y + size * 2 / 3;
  
  // Left part of check
  for (int i = 0; i < 3; i++) {
    renderer.drawLine(x + i, midY - size / 3 + i, midX + i, midY + i);
  }
  
  // Right part of check
  for (int i = 0; i < 3; i++) {
    renderer.drawLine(midX + i, midY + i, x + size + i, y + i);
  }
}

void AWSCertQuizActivity::drawXMark(int x, int y, int size) const {
  // Draw X mark
  for (int i = 0; i < 3; i++) {
    renderer.drawLine(x + i, y, x + size + i, y + size);
    renderer.drawLine(x + size - i, y, x - i, y + size);
  }
}

void AWSCertQuizActivity::drawPassBadge(int x, int y) const {
  int size = 120;
  
  // Rounded rectangle (double border)
  renderer.drawRect(x, y, size, size);
  renderer.drawRect(x + 2, y + 2, size - 4, size - 4);
  renderer.drawRect(x + 4, y + 4, size - 8, size - 8);
  
  // Checkmark
  drawCheckmark(x + size / 2 - 20, y + size / 2 - 15, 40);
  
  // "PASS" text
  renderer.drawText(UI_12_FONT_ID, x + size / 2 - 20, y + size + 10, fork_tr(STR_AWS_PASS), true);
}

void AWSCertQuizActivity::drawFailBadge(int x, int y) const {
  int size = 120;
  
  // Rounded rectangle (double border)
  renderer.drawRect(x, y, size, size);
  renderer.drawRect(x + 2, y + 2, size - 4, size - 4);
  renderer.drawRect(x + 4, y + 4, size - 8, size - 8);
  
  // X mark
  drawXMark(x + size / 2 - 20, y + size / 2 - 20, 40);
  
  // "FAIL" text
  renderer.drawText(UI_12_FONT_ID, x + size / 2 - 15, y + size + 10, fork_tr(STR_AWS_FAIL), true);
}

void AWSCertQuizActivity::renderDomainStats() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int height = renderer.getScreenHeight();  // used for button positioning
  const int rowHeight = 35;
  const int footerHeight = 40;
  // Reserve space for one potential "..." row plus the footer
  const int maxY = height - footerHeight - rowHeight;

  if (domainTotal.empty()) {
    renderer.drawText(UI_12_FONT_ID, margin, margin, fork_tr(STR_AWS_DOMAIN_BREAKDOWN), true);
    renderer.drawText(UI_10_FONT_ID, margin, margin + 50, fork_tr(STR_AWS_NO_DOMAIN_DATA), true);
    GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  renderer.drawText(UI_12_FONT_ID, margin, margin, fork_tr(STR_AWS_DOMAIN_BREAKDOWN), true);

  int y = margin + 50;
  bool truncated = false;
  int shown = 0;
  int totalDomains = (int)domainTotal.size();
  for (const auto& pair : domainTotal) {
    // Check if there is room for this row (and possibly a "..." row after it)
    if (y + rowHeight > maxY) {
      truncated = true;
      break;
    }

    const String& domain = pair.first;
    int total = pair.second;
    if (total == 0) continue;
    auto it = domainCorrect.find(domain);
    int correct = (it != domainCorrect.end()) ? it->second : 0;
    int percentage = (correct * 100) / total;

    char line[128];
    snprintf(line, sizeof(line), "%s: %d/%d (%d%%)",
             domain.c_str(), correct, total, percentage);
    renderer.drawText(UI_10_FONT_ID, margin, y, line, true);

    // Mini bar
    int barWidth = 200;
    int barFilled = (barWidth * percentage) / 100;
    renderer.drawRect(margin + 250, y + 5, barWidth, 10);
    renderer.fillRect(margin + 250, y + 5, barFilled, 10);

    shown++;
    y += rowHeight;
  }

  if (truncated) {
    char moreText[32];
    snprintf(moreText, sizeof(moreText), fork_tr(STR_AWS_AND_N_MORE), totalDomains - shown);
    renderer.drawText(UI_10_FONT_ID, margin, y, moreText, true);
  }

  GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

String AWSCertQuizActivity::getQuestionDomain(int actualIndex) {
  if (actualIndex >= 0 && actualIndex < (int)customQuestions.size()) {
    return String(customQuestions[actualIndex].domain.c_str());
  }
  return "General";
}

int AWSCertQuizActivity::drawWrappedText(int fontId, int x, int y, const char* text, int maxWidth, bool black) const {
  if (!text || maxWidth <= 0) return 0;
  
  const int startY = y;
  const int lineHeight = renderer.getLineHeight(fontId) + 2;
  char lineBuf[256];
  
  const char* lineStart = text;
  const char* ptr = text;
  const char* lastSpace = nullptr;
  
  while (*ptr) {
    if (*ptr == ' ') lastSpace = ptr;
    
    // Measure current line + next char
    int len = ptr - lineStart + 1;
    {
      int copyLen = (len < 256) ? len : 255;
      memcpy(lineBuf, lineStart, copyLen);
      lineBuf[copyLen] = '\0';
      int testWidth = renderer.getTextWidth(fontId, lineBuf);
      
      if (testWidth > maxWidth && ptr > lineStart) {
        // Line too long, break here
        const char* breakPoint = lastSpace ? lastSpace : ptr;
        len = breakPoint - lineStart;
        memcpy(lineBuf, lineStart, len);
        lineBuf[len] = '\0';
        renderer.drawText(fontId, x, y, lineBuf, black);
        
        y += lineHeight;
        lineStart = (*breakPoint == ' ') ? breakPoint + 1 : breakPoint;
        ptr = lineStart;
        lastSpace = nullptr;
        continue;
      }
    }
    ptr++;
  }
  
  // Draw remaining
  if (ptr > lineStart) {
    int len = ptr - lineStart;
    if (len < 256) {
      memcpy(lineBuf, lineStart, len);
      lineBuf[len] = '\0';
      renderer.drawText(fontId, x, y, lineBuf, black);
    }
    y += lineHeight;
  }
  
  return y - startY;
}
