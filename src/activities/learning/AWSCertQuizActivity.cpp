#include "../../DebugConfig.h"
#include "AWSCertQuizActivity.h"
#include "../../fontIds.h"
#include "../../QuizStatsManager.h"
#include <HalDisplay.h>
#include <SDCardManager.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <random>
#include <map>

// Constants
static constexpr int MAX_QUESTIONS_FROM_FILE = 200;  // Maximum questions to load from a single JSON file
static constexpr size_t JSON_PARSE_BUF_SIZE = 1536;
static constexpr size_t PATH_BUF_SIZE = 128;
static constexpr int DEFAULT_MARGIN = 20;
static constexpr int MIN_OPTION_HEIGHT = 30;

// All questions loaded from SD card JSON files
// Place JSON files in /aws-quiz/{cert-id}.json on SD card

void AWSCertQuizActivity::onEnter() {
  // Load questions from SD card
  hasCustomPack = loadCustomQuestions();
  
  if (!hasCustomPack || customQuestions.empty()) {
    // No questions - show error
    state = LOADING;
    showError("No Questions Found", 
              "Question bank files not found on SD card.\n\n"
              "Please add question files to:\n/aws-quiz/\n\n"
              "Format: JSON files with questions array");
    return;
  }
  
  // Questions loaded successfully
  
  // Set max questions based on mode
  if (practiceMode == "quick") {
    maxQuestions = 15;
  } else if (practiceMode == "study") {
    maxQuestions = 20;
  } else if (practiceMode == "quickstart") {
    maxQuestions = 5;  // Just 5 questions
  } else if (practiceMode == "review") {
    // Load incorrect questions from history
    if (!loadIncorrectHistory()) {
      showError("No Review History", 
                "No incorrect questions found.\n\n"
                "Complete a quiz first to build your review history.");
      return;
    }
    maxQuestions = questionCount;  // Use all incorrect questions
  } else {
    maxQuestions = getMaxQuestionsForCert();  // Full exam
  }
  
  // Enable timer for Full Exam mode
  showTimer = (practiceMode == "full");
  if (showTimer) {
    startTime = millis();
  }
  
  // Try to load existing session (only for full mode)
  if (practiceMode == "full" && loadSession()) {
    DEBUG_PRINTLN("[AWS] Resumed previous session");
    state = QUESTION;
    renderQuestion();
    return;
  }
  
  // Start new session (review mode already has questionOrder from loadIncorrectHistory)
  if (practiceMode != "review") {
    loadQuestions();
    shuffleQuestions();
  } else {
    // Review mode: initialize userAnswers for the loaded questionOrder
    userAnswers.clear();
    userAnswers.resize(questionCount, -1);
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
      if (customQuestions[i].domain == practiceDomain.c_str()) {
        filteredIndices.push_back(i);
      }
    }

    // Update questionOrder to only include filtered indices
    questionOrder = filteredIndices;
    questionCount = filteredIndices.size();
    DEBUG_PRINTF("[AWS] Filtered to %d questions for domain: %s\n", questionCount, practiceDomain.c_str());
    return;
  }
}

void AWSCertQuizActivity::shuffleQuestions() {
  // Build question index array
  questionOrder.clear();
  userAnswers.clear();
  questionOrder.reserve(questionCount);  // Pre-allocate to avoid reallocation
  userAnswers.reserve(questionCount);
  for (int i = 0; i < questionCount; i++) {
    questionOrder.push_back(i);
    userAnswers.push_back(-1);  // -1 means not answered yet
  }
  
  // Shuffle using random device
  std::random_device rd;
  std::mt19937 g(rd());
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
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s.dat", certId.c_str());
  FsFile file = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;

  bool ok = true;

  // Write session data
  ok = ok && file.write((uint8_t*)&currentIndex, sizeof(currentIndex)) == sizeof(currentIndex);
  ok = ok && file.write((uint8_t*)&questionCount, sizeof(questionCount)) == sizeof(questionCount);

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
    SdMan.remove(path);
    DEBUG_PRINTLN("[AWS] saveSession: partial write, removed corrupted file");
  }
}

bool AWSCertQuizActivity::loadSession() {
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s.dat", certId.c_str());
  FsFile file;
  if (!openFileOrShowError(path, file, "Session Load Error")) {
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
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-session-%s.dat", certId.c_str());
  SdMan.remove(path);
}

void AWSCertQuizActivity::saveIncorrectHistory() {
  if (incorrectQuestions.empty()) return;

  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-history-%s.dat", certId.c_str());
  FsFile file = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;
  
  // Write count
  int count = incorrectQuestions.size();
  file.write((uint8_t*)&count, sizeof(count));
  
  // Write incorrect question indices
  for (int idx : incorrectQuestions) {
    int actualIdx = questionOrder[idx];
    file.write((uint8_t*)&actualIdx, sizeof(actualIdx));
  }
  
  file.close();
  DEBUG_PRINTF("[AWS] Saved %d incorrect questions to history\n", count);
}

bool AWSCertQuizActivity::loadIncorrectHistory() {
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/.crosspoint/aws-quiz-history-%s.dat", certId.c_str());
  FsFile file;
  if (!SdMan.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTLN("[AWS] No incorrect question history found");
    return false;
  }
  
  // Read count
  int count;
  if (file.read((uint8_t*)&count, sizeof(count)) != sizeof(count)) {
    file.close();
    return false;
  }

  if (count <= 0 || count > 100) {
    file.close();
    return false;
  }

  // Read incorrect question indices
  questionOrder.clear();
  questionOrder.reserve(count);
  for (int i = 0; i < count; i++) {
    int idx;
    if (file.read((uint8_t*)&idx, sizeof(idx)) != sizeof(idx)) {
      file.close();
      questionOrder.clear();
      return false;
    }
    if (idx >= 0 && idx < (int)customQuestions.size()) {
      questionOrder.push_back(idx);
    }
  }
  
  file.close();
  
  questionCount = questionOrder.size();
  DEBUG_PRINTF("[AWS] Loaded %d incorrect questions from history\n", questionCount);
  return questionCount > 0;
}

bool AWSCertQuizActivity::openFileOrShowError(const char* path, FsFile& file, const char* errorTitle) {
  if (!SdMan.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTF("[AWS] Failed to open: %s\n", path);
    showError(errorTitle, "Could not open file.\n\nCheck SD card and file path.");
    return false;
  }
  return true;
}

uint16_t AWSCertQuizActivity::getCachedTextWidth(int fontId, const char* text) const {
  // Check cache
  for (int i = 0; i < 4; i++) {
    if (textWidthCache[i].text && strcmp(textWidthCache[i].text, text) == 0) {
      return textWidthCache[i].width;
    }
  }
  
  // Calculate and cache (LRU: shift and add to end)
  uint16_t width = renderer.getTextWidth(fontId, text);
  for (int i = 0; i < 3; i++) {
    textWidthCache[i] = textWidthCache[i + 1];
  }
  textWidthCache[3].text = text;
  textWidthCache[3].width = width;
  
  return width;
}

void AWSCertQuizActivity::saveQuizStats() {
  // Calculate domain scores
  std::map<String, QuizStatsManager::DomainScore> domainScores;
  
  for (int i = 0; i < questionCount; i++) {
    int actualIdx = questionOrder[i];
    const Question* q = &customQuestions[actualIdx];
    const std::string& domain = q->domain;

    String domainKey(domain.c_str());
    if (domainScores.find(domainKey) == domainScores.end()) {
      QuizStatsManager::DomainScore ds;
      strncpy(ds.domain, domain.c_str(), sizeof(ds.domain) - 1);
      ds.domain[sizeof(ds.domain) - 1] = '\0';  // Ensure null termination
      ds.correct = 0;
      ds.total = 0;
      domainScores[domainKey] = ds;
    }
    
    domainScores[domainKey].total++;
    if (userAnswers[i] == q->correct) {
      domainScores[domainKey].correct++;
    }
  }
  
  // Convert to vector
  std::vector<QuizStatsManager::DomainScore> domainVec;
  for (const auto& pair : domainScores) {
    domainVec.push_back(pair.second);
  }
  
  // Calculate total score
  int totalCorrect = 0;
  for (int i = 0; i < questionCount; i++) {
    int actualIdx = questionOrder[i];
    if (userAnswers[i] == customQuestions[actualIdx].correct) {
      totalCorrect++;
    }
  }

  // Save to stats manager
  QuizStatsManager::getInstance().saveQuizResult(
    certId.c_str(),
    practiceMode.c_str(),
    totalCorrect,
    questionCount,
    domainVec
  );

  DEBUG_PRINTF("[AWS] Saved quiz stats: %d/%d correct\n", totalCorrect, questionCount);
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
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool AWSCertQuizActivity::loadCustomQuestions() {
  char path[PATH_BUF_SIZE];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());
  DEBUG_PRINTF("[AWS] Trying to load: %s\n", path);

  FsFile file;
  if (!SdMan.openFileForRead("AWS", path, file)) {
    DEBUG_PRINTF("[AWS] Failed to open file: %s\n", path);
    return false;
  }

  DEBUG_PRINTF("[AWS] File opened, size: %d bytes\n", file.size());

  customQuestions.clear();

  // Find "questions" array by looking for the pattern
  bool foundArray = false;
  char searchBuf[32] = {0};
  int searchPos = 0;

  while (file.available() && !foundArray) {
    char c = file.read();

    // Shift buffer and add new char
    if (searchPos < 31) {
      searchBuf[searchPos++] = c;
    } else {
      memmove(searchBuf, searchBuf + 1, 30);
      searchBuf[30] = c;
    }

    // Look for "questions":[
    if (strstr(searchBuf, "\"questions\"") != NULL) {
      // Found "questions", now find the [
      while (file.available()) {
        c = file.read();
        if (c == '[') {
          foundArray = true;
          DEBUG_PRINTLN("[AWS] Found questions array");
          break;
        }
        if (c == '{' || c == '}') break;  // Wrong structure
      }
    }
  }

  if (!foundArray) {
    DEBUG_PRINTLN("[AWS] Could not find questions array");
    file.close();
    return false;
  }

  // Parse questions one at a time
  int parsedCount = 0;
  int braceDepth = 0;

  char* buffer = (char*)malloc(JSON_PARSE_BUF_SIZE);
  if (!buffer) {
    DEBUG_PRINTLN("[AWS] Failed to allocate parse buffer");
    file.close();
    return false;
  }

  int bufferPos = 0;

  DEBUG_PRINTF("[AWS] Free memory before parsing: %d bytes\n", ESP.getFreeHeap());

  while (file.available() && parsedCount < MAX_QUESTIONS_FROM_FILE) {
    char c = file.read();

    if (c == '{') {
      if (braceDepth == 0) bufferPos = 0;
      braceDepth++;
    }

    if (braceDepth > 0 && bufferPos < (int)JSON_PARSE_BUF_SIZE - 1) {
      buffer[bufferPos++] = c;
    }

    if (c == '}') {
      braceDepth--;
      if (braceDepth == 0 && bufferPos > 10) {
        buffer[bufferPos] = '\0';

        // Heap-allocated JSON document to avoid stack overflow
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, buffer);

        if (!error) {
          JsonObject q = doc.as<JsonObject>();

          // Validate required fields exist and are strings
          if (q["question"].is<const char*>() && !q["options"].isNull() && q["options"].size() >= 4 &&
              q["options"][0].is<const char*>() && q["options"][1].is<const char*>() &&
              q["options"][2].is<const char*>() && q["options"][3].is<const char*>()) {
            Question question;
            question.certId = certId.c_str();
            question.question = q["question"].as<const char*>();
            question.options[0] = q["options"][0].as<const char*>();
            question.options[1] = q["options"][1].as<const char*>();
            question.options[2] = q["options"][2].as<const char*>();
            question.options[3] = q["options"][3].as<const char*>();
            question.correct = std::min(static_cast<uint8_t>(q["correct"] | 0), static_cast<uint8_t>(3));
            question.explanation = q["explanation"] | "No explanation available";
            question.domain = q["domain"] | "General";
            customQuestions.push_back(std::move(question));
            parsedCount++;
          } else {
            DEBUG_PRINTF("[AWS] Skipped invalid question at position %d\n", parsedCount);
          }
        } else {
          DEBUG_PRINTF("[AWS] JSON parse error at question %d: %s\n", parsedCount, error.c_str());
        }

        // Check memory every 10 questions
        if (parsedCount % 10 == 0) {
          DEBUG_PRINTF("[AWS] Free memory after %d questions: %d bytes\n", parsedCount, ESP.getFreeHeap());
        }
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
  
  // If this question was already answered, show that answer
  if (userAnswers[currentIndex] >= 0) {
    selectedOption = userAnswers[currentIndex];
  }
  
  // AWS Logo (simple smile)
  drawAWSLogo(width - 60, 15);
  
  // Streak indicator (if active)
  if (currentStreak >= 2) {
    char streakText[32];
    snprintf(streakText, sizeof(streakText), "%d in a row!", currentStreak);
    renderer.drawText(UI_10_FONT_ID, width - 120, 35, streakText, true);
  }
  
  // Timer (Full Exam mode only)
  if (showTimer) {
    unsigned long elapsed = (millis() - startTime) / 1000;  // seconds
    int minutes = elapsed / 60;
    int seconds = elapsed % 60;
    char timerText[16];
    snprintf(timerText, sizeof(timerText), "%02d:%02d", minutes, seconds);
    renderer.drawText(UI_10_FONT_ID, width - 80, 15, timerText, true);
  }
  
  // Progress bar
  int progressBarWidth = width - 2 * margin;
  int progressFilled = (progressBarWidth * (currentIndex + 1)) / questionCount;
  renderer.drawRect(margin, 15, progressBarWidth, 8);
  renderer.fillRect(margin, 15, progressFilled, 8);
  
  // Progress text
  char progress[32];
  snprintf(progress, sizeof(progress), "Question %d/%d", currentIndex + 1, questionCount);
  renderer.drawText(UI_10_FONT_ID, margin, 30, progress, true);
  
  // Difficulty indicator (simple stars - assume medium for embedded)
  renderer.drawText(UI_10_FONT_ID, margin + 150, 30, "**", true);
  
  // Question text (wrapped)
  int y = 60;
  int textHeight = drawWrappedText(UI_10_FONT_ID, margin, y, q->question.c_str(), width - 2 * margin);
  y += textHeight + 10;
  
  // Options
  for (int i = 0; i < 4; i++) {
    int optionStartY = y;
    int optionHeight = drawWrappedText(UI_10_FONT_ID, margin + 10, y + 5, q->options[i].c_str(), width - 2 * margin - 20, true) - y;

    // Ensure minimum height for selection box
    if (optionHeight < MIN_OPTION_HEIGHT) optionHeight = MIN_OPTION_HEIGHT;
    
    // In study mode, highlight both selected and correct answers
    bool isSelected = (i == selectedOption);
    bool isCorrect = (i == q->correct);
    bool shouldHighlight = isSelected || (practiceMode == "study" && isCorrect);
    
    if (shouldHighlight) {
      // Rounded rectangle effect
      renderer.fillRect(margin + 2, optionStartY - 5, width - 2 * margin - 4, optionHeight + 10);
      renderer.fillRect(margin, optionStartY - 3, width - 2 * margin, optionHeight + 6);
      // Redraw text in white (inverted)
      drawWrappedText(UI_10_FONT_ID, margin + 10, optionStartY + 5, q->options[i].c_str(), width - 2 * margin - 20, false);
      
      // Study mode: show icons
      if (practiceMode == "study") {
        int iconX = width - margin - 30;
        int iconY = optionStartY + (optionHeight / 2) - 10;
        if (isCorrect) {
          drawCheckmark(iconX, iconY, 20);
        } else if (isSelected) {
          drawXMark(iconX, iconY, 20);
        }
      }
    }
    
    y += optionHeight + 15;
  }
  
  // Button hints (match physical button layout)
  const char* btn1 = "Back";
  const char* btn2;
  const char* btn3 = currentIndex > 0 ? "Prev" : "";
  const char* btn4;
  
  if (practiceMode == "study") {
    // Study mode: answer shown immediately, can view explanation
    btn2 = "Explain";
    btn4 = currentIndex < questionCount - 1 ? "Next" : "Finish";
  } else {
    // Exam mode: just navigate, no peeking
    btn2 = "";
    btn4 = currentIndex < questionCount - 1 ? "Next" : "Submit";
  }
  
  renderer.drawButtonHints(UI_10_FONT_ID, btn1, btn2, btn3, btn4);
  
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
  bool correct = (selectedOption == q->correct);
  
  // Result with icon
  int iconX = margin;
  int iconY = margin;
  if (correct) {
    drawCheckmark(iconX, iconY, 30);
    renderer.drawText(UI_12_FONT_ID, iconX + 40, iconY + 5, "Correct!", true);
    
    // Encouragement for streaks
    if (currentStreak == 3) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, "On fire!", true);
    } else if (currentStreak == 5) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, "Unstoppable!", true);
    } else if (currentStreak >= 7) {
      renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, "Perfect!", true);
    }
  } else {
    drawXMark(iconX, iconY, 30);
    renderer.drawText(UI_12_FONT_ID, iconX + 40, iconY + 5, "Incorrect", true);
    renderer.drawText(UI_10_FONT_ID, iconX + 150, iconY + 5, "Keep learning!", true);
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
  
  // Score with visual indicator
  char scoreText[64];
  snprintf(scoreText, sizeof(scoreText), "Score: %d/%d", correctCount, currentIndex + 1);
  renderer.drawText(UI_10_FONT_ID, margin, y, scoreText, true);
  
  // Score bar
  int scoreBarWidth = 150;
  int scoreFilled = (scoreBarWidth * correctCount) / (currentIndex + 1);
  renderer.drawRect(margin + 100, y + 5, scoreBarWidth, 10);
  renderer.fillRect(margin + 100, y + 5, scoreFilled, 10);
  
  // Button hints (match physical button layout)
  const char* btn1 = "Back";
  const char* btn2 = currentIndex < questionCount - 1 ? "Continue" : "Summary";
  const char* btn3 = currentIndex > 0 ? "Prev" : "";
  const char* btn4 = currentIndex < questionCount - 1 ? "Next" : "";
  renderer.drawButtonHints(UI_10_FONT_ID, btn1, btn2, btn3, btn4);
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::renderSummary() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();  // used for button positioning
  
  int percentage = (correctCount * 100) / questionCount;
  bool passed = percentage >= 70;
  
  // AWS Logo
  drawAWSLogo(width / 2 - 20, margin);
  
  // Title
  renderer.drawText(UI_12_FONT_ID, margin, margin + 50, "Quiz Complete!", true);
  
  // Pass/Fail Badge
  int badgeY = margin + 90;
  int badgeX = width / 2 - 60;
  if (passed) {
    drawPassBadge(badgeX, badgeY);
  } else {
    drawFailBadge(badgeX, badgeY);
  }
  
  // Score
  int y = badgeY + 140;
  char scoreText[64];
  snprintf(scoreText, sizeof(scoreText), "Final Score: %d/%d", correctCount, questionCount);
  renderer.drawText(UI_12_FONT_ID, margin, y, scoreText, true);
  y += 40;
  
  char percentText[32];
  snprintf(percentText, sizeof(percentText), "(%d%%)", percentage);
  renderer.drawText(UI_10_FONT_ID, margin, y, percentText, true);
  y += 40;
  
  // Score visualization
  int barWidth = width - 2 * margin;
  int barHeight = 30;
  renderer.drawRect(margin, y, barWidth, barHeight);
  
  // Fill based on percentage
  int fillWidth = (barWidth * percentage) / 100;
  renderer.fillRect(margin, y, fillWidth, barHeight);
  
  // Percentage markers
  renderer.drawLine(margin + (barWidth * 70) / 100, y - 5, margin + (barWidth * 70) / 100, y + barHeight + 5);
  renderer.drawText(UI_10_FONT_ID, margin + (barWidth * 70) / 100 - 10, y + barHeight + 15, "70%", true);
  
  y += 60;
  
  // Streak achievement
  if (longestStreak >= 3) {
    char streakText[64];
    snprintf(streakText, sizeof(streakText), "Longest Streak: %d in a row!", longestStreak);
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
    snprintf(reviewText, sizeof(reviewText), "Review Mistakes (%d)", incorrectQuestions.size());
    renderer.drawRect(margin, y, 200, 35);
    renderer.drawText(UI_10_FONT_ID, margin + 10, y + 10, reviewText, true);
    y += 45;
  }
  
  // Domain stats option
  renderer.drawRect(margin, y, 200, 35);
  renderer.drawText(UI_10_FONT_ID, margin + 10, y + 10, "Domain Breakdown", true);
  
  const char* legend = incorrectQuestions.size() > 0 ? 
                       "Back | Confirm: Review | Down: Domains" : "Back | Down: Domains";
  renderer.drawText(UI_10_FONT_ID, margin, height - 40, legend, true);
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::renderReview() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int width = renderer.getScreenWidth();

  if (reviewIndex < 0 || reviewIndex >= (int)incorrectQuestions.size()) return;
  int questionIdx = incorrectQuestions[reviewIndex];
  if (questionIdx < 0 || questionIdx >= (int)questionOrder.size()) return;
  int actualIndex = questionOrder[questionIdx];
  if (actualIndex < 0 || actualIndex >= (int)customQuestions.size()) return;
  const Question* q = &customQuestions[actualIndex];
  int userAnswer = userAnswers[questionIdx];
  
  // Header
  int y = margin;
  char header[64];
  snprintf(header, sizeof(header), "Review %d/%d", reviewIndex + 1, (int)incorrectQuestions.size());
  renderer.drawText(UI_12_FONT_ID, margin, y, header, true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  
  renderer.drawLine(margin, y, width - margin, y);
  y += 20;
  
  // Question
  y = drawWrappedText(UI_10_FONT_ID, margin, y, q->question.c_str(), width - 2 * margin);
  y += 20;

  // Show all options with indicators
  for (int i = 0; i < 4; i++) {
    int optionY = y;

    // Indicator
    if (i == q->correct) {
      drawCheckmark(margin, optionY, 15);
      renderer.drawText(UI_10_FONT_ID, margin + 20, optionY, "Correct:", true);
    } else if (i == userAnswer) {
      drawXMark(margin, optionY, 15);
      renderer.drawText(UI_10_FONT_ID, margin + 20, optionY, "Your answer:", true);
    }

    y += 20;
    y = drawWrappedText(UI_10_FONT_ID, margin + 20, y, q->options[i].c_str(), width - 2 * margin - 20);
    y += 10;
  }

  y += 10;

  // Explanation
  renderer.drawText(UI_10_FONT_ID, margin, y, "Explanation:", true);
  y += 20;
  y = drawWrappedText(UI_10_FONT_ID, margin, y, q->explanation.c_str(), width - 2 * margin);
  
  const char* btn2 = reviewIndex < incorrectQuestions.size() - 1 ? "Next" : "Done";
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", btn2, "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertQuizActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (state == LOADING) {
    // Waiting for user to press back (error state)
    return;
  } else if (state == QUESTION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedOption = (selectedOption + 3) % 4;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selectedOption = (selectedOption + 1) % 4;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      // Previous question
      if (currentIndex > 0) {
        currentIndex--;
        saveSession();  // Save position
        renderQuestion();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Next question (or submit if last)
      userAnswers[currentIndex] = selectedOption;  // Save answer
      
      if (currentIndex < questionCount - 1) {
        currentIndex++;
        selectedOption = 0;
        saveSession();  // Save progress
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

          // Populate domain maps
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
        state = SUMMARY;
        renderSummary();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Study mode: show answer immediately
      // Exam mode: Confirm does nothing (just navigate)
      if (practiceMode == "study") {
        userAnswers[currentIndex] = selectedOption;  // Save answer
        state = ANSWER;
        renderAnswer();
      }
    }
  } else if (state == ANSWER) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Return to question view (study mode only)
      state = QUESTION;
      renderQuestion();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      // Previous question
      if (currentIndex > 0) {
        currentIndex--;
        state = QUESTION;
        renderQuestion();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Next question
      if (currentIndex < questionCount - 1) {
        currentIndex++;
        selectedOption = 0;
        state = QUESTION;
        renderQuestion();
      } else {
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
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = SUMMARY;
      renderSummary();
    }
  } else if (state == REVIEW) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      reviewIndex++;
      if (reviewIndex < incorrectQuestions.size()) {
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
  renderer.drawText(UI_12_FONT_ID, x + size / 2 - 20, y + size + 10, "PASS", true);
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
  renderer.drawText(UI_12_FONT_ID, x + size / 2 - 15, y + size + 10, "FAIL", true);
}

void AWSCertQuizActivity::renderDomainStats() const {
  renderer.clearScreen();

  const int margin = DEFAULT_MARGIN;
  const int height = renderer.getScreenHeight();  // used for button positioning
  
  renderer.drawText(UI_12_FONT_ID, margin, margin, "Domain Breakdown", true);
  
  int y = margin + 50;
  for (const auto& pair : domainTotal) {
    const String& domain = pair.first;
    int total = pair.second;
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
    
    y += 35;
  }
  
  renderer.drawText(UI_10_FONT_ID, margin, height - 40, "Back: Return", true);
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
    if (len < 256) {
      memcpy(lineBuf, lineStart, len);
      lineBuf[len] = '\0';
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
