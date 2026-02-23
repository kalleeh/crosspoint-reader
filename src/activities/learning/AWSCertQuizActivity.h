#pragma once

#include "../Activity.h"
#include "../../MappedInputManager.h"
#include <GfxRenderer.h>
#include <functional>
#include <map>
#include <vector>
#include <string>

struct Question {
  const char* certId = nullptr;  // Non-owning pointer (lifetime managed by AWSCertQuizActivity::certId)
  std::string question;
  std::string options[4];
  uint8_t correct = 0;
  std::string explanation;
  std::string domain;
  std::string difficulty;  // "easy", "medium", "hard"
};

class AWSCertQuizActivity final : public Activity {
  enum State { LOADING, QUESTION, ANSWER, SUMMARY, REVIEW, DOMAIN_STATS };
  
  const std::function<void()> onBack;
  String certId;
  String practiceMode;  // "full", "quick", "study", "domain", "review"
  String practiceDomain;  // Domain filter for domain mode
  State state = LOADING;
  
  int questionStartIndex = 0;
  int questionCount = 0;
  int currentIndex = 0;
  int selectedOption = -1;  // -1 = no selection (cursor inactive on fresh question)
  int correctCount = 0;
  
  // Custom question pack support (SD card only)
  std::vector<Question> customQuestions;
  bool hasCustomPack = false;
  
  // Randomization support (optimized for memory)
  std::vector<uint16_t> questionOrder;  // uint16_t instead of int (saves 2 bytes each)
  std::vector<int8_t> userAnswers;      // int8_t instead of int (saves 3 bytes each, -1 = not answered)
  int maxQuestions = 15;  // Default quiz length
  
  // Engagement features
  int currentStreak = 0;
  int longestStreak = 0;
  
  // Timer (for Full Exam mode)
  unsigned long startTime = 0;
  unsigned long endTime = 0;
  bool showTimer = false;
  
  // Review mode
  std::vector<int> incorrectQuestions;
  int reviewIndex = 0;
  
  // Domain tracking
  std::map<String, int> domainCorrect;
  std::map<String, int> domainTotal;
  
  // Text measurement cache (80 bytes)
  struct TextCache {
    const char* text = nullptr;
    uint16_t width = 0;
  };
  mutable TextCache textWidthCache[4];  // Small LRU cache
  
  uint16_t getCachedTextWidth(int fontId, const char* text) const;
  
  void loadQuestions();
  bool loadCustomQuestions();
  void shuffleQuestions();
  void saveSession();
  bool loadSession();
  void clearSession();
  void saveIncorrectHistory();
  bool loadIncorrectHistory();
  void saveQuizStats();
  void showError(const char* title, const char* message);
  bool openFileOrShowError(const char* path, FsFile& file, const char* errorTitle);
  void renderQuestion();
  void renderAnswer() const;
  void renderSummary() const;
  void renderReview() const;
  void renderDomainStats() const;
  String getQuestionDomain(int index);
  
  // UI helpers
  void drawAWSLogo(int x, int y) const;
  void drawCheckmark(int x, int y, int size) const;
  void drawXMark(int x, int y, int size) const;
  void drawPassBadge(int x, int y) const;
  void drawFailBadge(int x, int y) const;
  int drawWrappedText(int fontId, int x, int y, const char* text, int maxWidth, bool black = true) const;
  
 public:
  explicit AWSCertQuizActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onBack,
                               const char* certId,
                               const char* mode = "full",
                               const char* domain = "all")
      : Activity("AWS Quiz", renderer, mappedInput), 
        onBack(onBack), certId(certId), practiceMode(mode), practiceDomain(domain) {}
  
  void onEnter() override;
  void loop() override;
  
 private:
  int getMaxQuestionsForCert() const {
    // Return actual exam question counts per certification
    if (certId == "cloud-practitioner") return 65;
    if (certId == "ai-practitioner") return 65;
    if (certId == "sa-associate") return 65;
    if (certId == "developer-associate") return 65;
    if (certId == "sysops-associate") return 65;
    if (certId == "sa-professional") return 75;
    if (certId == "devops-professional") return 75;
    if (certId == "security-specialty") return 65;
    if (certId == "ml-specialty") return 65;
    if (certId == "database-specialty") return 65;
    if (certId == "networking-specialty") return 65;
    if (certId == "analytics-specialty") return 65;
    if (certId == "sap-specialty") return 65;
    return 50;  // Default fallback
  }

  int getPassingScoreForCert() const {
    // Return passing percentage threshold per certification
    if (certId == "cloud-practitioner") return 70;
    if (certId == "ai-practitioner") return 70;
    if (certId == "sa-associate") return 72;
    if (certId == "developer-associate") return 72;
    if (certId == "sysops-associate") return 72;
    if (certId == "sa-professional") return 75;
    if (certId == "devops-professional") return 75;
    if (certId == "security-specialty") return 75;
    if (certId == "ml-specialty") return 75;
    if (certId == "database-specialty") return 75;
    if (certId == "networking-specialty") return 75;
    if (certId == "analytics-specialty") return 75;
    if (certId == "sap-specialty") return 75;
    return 70;  // Default fallback
  }
};
