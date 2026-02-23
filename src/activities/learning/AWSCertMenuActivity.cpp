#include "../../DebugConfig.h"
#include "AWSCertMenuActivity.h"
#include "../../fontIds.h"
#include "../../ScreenComponents.h"
#include "../../QuizStatsManager.h"
#include <HalDisplay.h>
#include <time.h>

void AWSCertMenuActivity::onEnter() {
  render();
}

void AWSCertMenuActivity::render() {
  if (showingInfo) {
    renderInfo();
    return;
  }
  
  renderer.clearScreen();
  
  const int margin = 20;
  const int width = renderer.getScreenWidth();
  
  // Title
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, "AWS Certifications", true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  
  // Tab bar
  std::vector<TabInfo> tabs = {
    {"Certifications", currentTab == Tab::Certifications},
    {"Stats", currentTab == Tab::Stats}
  };
  ScreenComponents::drawTabBar(renderer, y, tabs);
  y += 40;  // Tab bar height
  
  // Render current tab content
  if (currentTab == Tab::Certifications) {
    renderCertificationsTab();
  } else {
    renderStatsTab();
  }
}

void AWSCertMenuActivity::renderCertificationsTab() {
  const int margin = 20;
  const int lineHeight = 35;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int startY = 100;  // After title + tabs
  
  int y = startY;
  
  // Ensure selected item is visible
  const int visibleItems = (height - y - 60) / lineHeight;
  if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  } else if (selectedIndex >= scrollOffset + visibleItems) {
    scrollOffset = selectedIndex - visibleItems + 1;
  }
  
  // Draw cert options
  for (int i = scrollOffset; i < certs.size() && y < height - 60; i++) {
    bool isSelected = (i == selectedIndex);
    
    if (isSelected) {
      renderer.fillRect(margin, y, width - 2 * margin, lineHeight);
    }
    
    // Center text vertically in the box
    int textY = y + (lineHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, margin + 10, textY, certs[i].name, !isSelected);
    y += lineHeight;
  }
  
  // Button hints
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Start", "Info", "");
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertMenuActivity::loop() {
  if (showingInfo) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || 
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      showingInfo = false;
      render();
    }
  } else if (currentTab == Tab::Stats) {
    // Stats tab - only Back button
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onBack();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Switch to Certifications tab
      currentTab = Tab::Certifications;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Clear all stats
      if (QuizStatsManager::getInstance().getTotalQuestionsAnswered() > 0) {
        QuizStatsManager::getInstance().clearAllStats();
        render();
      }
    }
  } else {
    // Certifications tab
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onBack();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedIndex > 0) {
        selectedIndex--;
        render();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (selectedIndex < certs.size() - 1) {
        selectedIndex++;
        render();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      showingInfo = true;
      renderInfo();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Switch to Stats tab
      currentTab = Tab::Stats;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      onSelectCert(certs[selectedIndex].id);
    }
  }
}

void AWSCertMenuActivity::renderStatsTab() {
  const int margin = 20;
  const int width = renderer.getScreenWidth();
  const int startY = 100;  // After title + tabs
  
  auto& stats = QuizStatsManager::getInstance();
  
  int y = startY;
  
  // Streak
  int streak = stats.getStreak();
  char streakText[32];
  snprintf(streakText, sizeof(streakText), "🔥 %d Day Streak", streak);
  renderer.drawText(UI_12_FONT_ID, margin, y, streakText, true);
  y += 40;
  
  // Overall Progress
  renderer.drawText(UI_10_FONT_ID, margin, y, "Overall Progress:", true);
  y += 25;
  
  int totalQuestions = stats.getTotalQuestionsAnswered();
  char questionsText[64];
  snprintf(questionsText, sizeof(questionsText), "• Questions Answered: %d", totalQuestions);
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, questionsText, true);
  y += 20;
  
  int avgScore = stats.getAverageScore();
  char scoreText[64];
  if (totalQuestions > 0) {
    snprintf(scoreText, sizeof(scoreText), "• Average Score: %d%%", avgScore);
  } else {
    snprintf(scoreText, sizeof(scoreText), "• Average Score: --");
  }
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, scoreText, true);
  y += 30;
  
  // Recent History
  renderer.drawText(UI_10_FONT_ID, margin, y, "Recent History:", true);
  y += 25;
  
  auto history = stats.getRecentHistory(5);
  if (history.empty()) {
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, "No quiz history yet", true);
    y += 20;
  } else {
    for (const auto& result : history) {
      char historyText[128];
      int percentage = (result.score * 100) / result.total;
      
      // Format date
      struct tm* timeinfo = localtime((time_t*)&result.timestamp);
      char dateStr[16];
      if (timeinfo) {
        strftime(dateStr, sizeof(dateStr), "%b %d", timeinfo);
      } else {
        strncpy(dateStr, "Unknown", sizeof(dateStr));
      }
      
      snprintf(historyText, sizeof(historyText), "• %s: %s (%d%%)", 
               dateStr, result.mode, percentage);
      renderer.drawText(UI_10_FONT_ID, margin + 10, y, historyText, true);
      y += 20;
    }
  }
  y += 10;
  
  // Weak Areas
  renderer.drawText(UI_10_FONT_ID, margin, y, "Weak Areas:", true);
  y += 25;
  
  auto weakDomains = stats.getWeakDomains(5);
  if (weakDomains.empty()) {
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, "No weak areas yet", true);
  } else {
    int count = 0;
    for (const auto& pair : weakDomains) {
      if (count >= 3) break;  // Show top 3
      
      char weakText[128];
      snprintf(weakText, sizeof(weakText), "• %s: %d%%", 
               pair.first.c_str(), pair.second);
      renderer.drawText(UI_10_FONT_ID, margin + 10, y, weakText, true);
      y += 20;
      count++;
    }
  }
  
  // Clear stats option (if there's data)
  if (stats.getTotalQuestionsAnswered() > 0) {
    y += 20;
    renderer.drawRect(margin, y, 150, 30);
    renderer.drawText(UI_10_FONT_ID, margin + 10, y + 8, "Clear All Stats", true);
  }
  
  // Button hints
  const char* hint = stats.getTotalQuestionsAnswered() > 0 ? "Back | Confirm: Clear" : "Back";
  renderer.drawButtonHints(UI_10_FONT_ID, hint, "", "", "");
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertMenuActivity::renderInfo() {
  renderer.clearScreen();
  
  const int margin = 20;
  const int width = renderer.getScreenWidth();

  auto& cert = certs[selectedIndex];
  
  // Title
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, cert.name, true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;  // Space below title
  
  renderer.drawLine(margin, y, width - margin, y);
  y += 20;  // Space below divider
  
  // Level badge
  renderer.drawRect(margin, y, 120, 25);
  renderer.drawText(UI_10_FONT_ID, margin + 10, y + 5, cert.level, true);
  y += 35;
  
  // Exam code
  char line[128];
  snprintf(line, sizeof(line), "Exam: %s", cert.examCode);
  renderer.drawText(UI_10_FONT_ID, margin, y, line, true);
  y += 25;
  
  // Questions & passing
  snprintf(line, sizeof(line), "Questions: %d | Pass: %d%%", cert.questionCount, cert.passingScore);
  renderer.drawText(UI_10_FONT_ID, margin, y, line, true);
  y += 25;
  
  // Duration
  renderer.drawText(UI_10_FONT_ID, margin, y, "Duration: 90-180 minutes", true);
  y += 30;
  
  // Target audience
  renderer.drawText(UI_10_FONT_ID, margin, y, "Target Audience:", true);
  y += 20;
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, cert.audience, true);
  y += 30;
  
  // Domain breakdown
  renderer.drawText(UI_10_FONT_ID, margin, y, "Exam Domains:", true);
  y += 20;
  
  // Parse and display domains (split by |)
  String domains = String(cert.domains);
  int start = 0;
  int pipePos = domains.indexOf('|');
  while (pipePos != -1) {
    String domain = domains.substring(start, pipePos);
    domain.trim();
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, domain.c_str(), true);
    y += 20;
    start = pipePos + 1;
    pipePos = domains.indexOf('|', start);
  }
  // Last domain
  String lastDomain = domains.substring(start);
  lastDomain.trim();
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, lastDomain.c_str(), true);
  y += 30;
  
  // Practice info
  renderer.drawText(UI_10_FONT_ID, margin, y, "This quiz helps you prepare", true);
  y += 20;
  renderer.drawText(UI_10_FONT_ID, margin, y, "with practice questions.", true);
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Start Quiz", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
