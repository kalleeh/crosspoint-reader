#include "../../DebugConfig.h"
#include "AWSCertMenuActivity.h"
#include "../../fontIds.h"
#include "components/UITheme.h"
#include "../../QuizStatsManager.h"
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <ForkI18n.h>
#include <time.h>

void AWSCertMenuActivity::onEnter() {
  // Check once which cert JSON files are present on the SD card
  certFileAvailable.clear();
  for (const auto& cert : certs) {
    char path[128];
    snprintf(path, sizeof(path), "/aws-quiz/%s.json", cert.id);
    certFileAvailable.push_back(Storage.exists(path));
  }
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
  renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_AWS_MENU_TITLE), true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

  // Tab bar
  std::vector<TabInfo> tabs = {
    {fork_tr(STR_AWS_TAB_CERTS), currentTab == Tab::Certifications},
    {fork_tr(STR_AWS_TAB_STATS), currentTab == Tab::Stats}
  };
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawTabBar(renderer, Rect{0, y, width, metrics.tabBarHeight}, tabs, false);
  y += metrics.tabBarHeight;
  
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
    if (i < (int)certFileAvailable.size() && !certFileAvailable[i]) {
      char labelBuf[128];
      snprintf(labelBuf, sizeof(labelBuf), "[?] %s", certs[i].name);
      renderer.drawText(UI_10_FONT_ID, margin + 10, textY, labelBuf, !isSelected);
    } else {
      renderer.drawText(UI_10_FONT_ID, margin + 10, textY, certs[i].name, !isSelected);
    }
    y += lineHeight;
  }
  
  // Button hints
  GUI.drawButtonHints(renderer, tr(STR_BACK), fork_tr(STR_BTN_START), fork_tr(STR_BTN_INFO), fork_tr(STR_BTN_STATS));
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSCertMenuActivity::loop() {
  if (showingInfo) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showingInfo = false;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      showingInfo = false;
      onSelectCert(certs[selectedIndex].id);
    }
  } else if (currentTab == Tab::Stats) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (confirmingClear) {
        // Cancel the pending clear instead of leaving the tab
        confirmingClear = false;
        render();
      } else {
        onBack();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      // Switch to Certifications tab (also cancels any pending clear)
      confirmingClear = false;
      currentTab = Tab::Certifications;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Clearing wipes stats for ALL certs — require a second Confirm
      if (QuizStatsManager::getInstance().getTotalQuestionsAnswered(certs[selectedIndex].id) > 0) {
        if (confirmingClear) {
          confirmingClear = false;
          QuizStatsManager::getInstance().clearAllStats();
        } else {
          confirmingClear = true;
        }
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
  if (selectedIndex < 0 || selectedIndex >= (int)certs.size()) selectedIndex = 0;

  const int margin = 20;
  const int width = renderer.getScreenWidth();
  const int startY = 100;  // After title + tabs

  auto& stats = QuizStatsManager::getInstance();
  
  int y = startY;
  
  // Streak
  int streak = stats.getStreak(certs[selectedIndex].id);
  char streakText[32];
  snprintf(streakText, sizeof(streakText), "Streak: %d days", streak);
  renderer.drawText(UI_12_FONT_ID, margin, y, streakText, true);
  y += 40;
  
  // Overall Progress
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_OVERALL_PROGRESS), true);
  y += 25;

  int totalQuestions = stats.getTotalQuestionsAnswered(certs[selectedIndex].id);
  char questionsText[64];
  snprintf(questionsText, sizeof(questionsText), "%s %d", fork_tr(STR_AWS_QUESTIONS_ANSWERED), totalQuestions);
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, questionsText, true);
  y += 20;

  int avgScore = stats.getAverageScore(certs[selectedIndex].id);
  char scoreText[64];
  if (totalQuestions > 0) {
    snprintf(scoreText, sizeof(scoreText), "%s %d%%", fork_tr(STR_AWS_AVERAGE_SCORE), avgScore);
  } else {
    snprintf(scoreText, sizeof(scoreText), "%s", fork_tr(STR_AWS_AVERAGE_SCORE_NA));
  }
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, scoreText, true);
  y += 30;

  // Recent History
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_RECENT_HISTORY), true);
  y += 25;

  auto history = stats.getRecentHistory(5, certs[selectedIndex].id);
  if (history.empty()) {
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, fork_tr(STR_AWS_NO_HISTORY), true);
    y += 20;
  } else {
    for (const auto& result : history) {
      char historyText[128];
      int percentage = (result.score * 100) / result.total;
      
      // Format date (copy to a time_t: the field is a packed uint32_t, and
      // time_t is 64-bit — casting the pointer would read past the field
      // and do an unaligned load)
      time_t ts = result.timestamp;
      struct tm* timeinfo = localtime(&ts);
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
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_WEAK_AREAS), true);
  y += 25;

  auto weakDomains = stats.getWeakDomains(5, certs[selectedIndex].id);
  if (weakDomains.empty()) {
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, fork_tr(STR_AWS_NO_WEAK_AREAS), true);
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
  
  // Clear stats option (if there's data for this cert)
  if (stats.getTotalQuestionsAnswered(certs[selectedIndex].id) > 0) {
    y += 20;
    if (confirmingClear) {
      // Destructive action pending — show the warning where the button was
      renderer.fillRect(margin, y, 300, 30);
      renderer.drawText(UI_10_FONT_ID, margin + 10, y + 8, fork_tr(STR_AWS_CLEAR_CONFIRM), false);
    } else {
      renderer.drawRect(margin, y, 150, 30);
      renderer.drawText(UI_10_FONT_ID, margin + 10, y + 8, fork_tr(STR_AWS_CLEAR_STATS), true);
    }
  }

  // Button hints
  const char* clearHint = stats.getTotalQuestionsAnswered(certs[selectedIndex].id) > 0 ? fork_tr(STR_BTN_CLEAR_ALL) : "";
  GUI.drawButtonHints(renderer, tr(STR_BACK), clearHint, "", "");
  
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
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_EXAM_DURATION), true);
  y += 30;

  // Target audience
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_TARGET_AUDIENCE), true);
  y += 20;
  renderer.drawText(UI_10_FONT_ID, margin + 10, y, cert.audience, true);
  y += 30;

  // Domain breakdown
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_EXAM_DOMAINS), true);
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
  renderer.drawText(UI_10_FONT_ID, margin, y, fork_tr(STR_AWS_PRACTICE_INFO), true);
  
  GUI.drawButtonHints(renderer, tr(STR_BACK), fork_tr(STR_BTN_PRACTICE), "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
