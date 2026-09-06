#include "AWSPracticeModeActivity.h"

#include <ArduinoJson.h>
#include <ForkI18n.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "../../DebugConfig.h"
#include "../../fontIds.h"
#include "components/UITheme.h"

void AWSPracticeModeActivity::onEnter() { render(); }

void AWSPracticeModeActivity::render() {
  renderer.clearScreen();

  const int margin = 20;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  // Title
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_AWS_SELECT_MODE), true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

  renderer.drawLine(margin, y, width - margin, y);
  y += 20;

  // Calculate item heights dynamically
  std::vector<int> itemHeights;
  for (const auto& mode : modes) {
    int itemHeight = 10;                                  // Top padding
    itemHeight += renderer.getLineHeight(UI_10_FONT_ID);  // Name
    itemHeight += 5;                                      // Spacing
    itemHeight += renderer.getLineHeight(UI_10_FONT_ID);  // Description
    itemHeight += 10;                                     // Bottom padding
    itemHeights.push_back(itemHeight);
  }

  // Draw mode options
  for (int i = 0; i < modes.size() && y < height - 60; i++) {
    bool isSelected = (i == selectedIndex);
    int itemHeight = itemHeights[i];

    if (isSelected) {
      renderer.fillRect(margin, y, width - 2 * margin, itemHeight);
    }

    // Mode name
    int textY = y + 10;
    renderer.drawText(UI_10_FONT_ID, margin + 10, textY, modes[i].name, !isSelected);

    // Description
    textY += renderer.getLineHeight(UI_10_FONT_ID) + 5;
    renderer.drawText(UI_10_FONT_ID, margin + 10, textY, modes[i].description, !isSelected);

    y += itemHeight;
  }

  if (statusMessage.length() > 0) {
    renderer.drawText(UI_10_FONT_ID, margin, height - 60, statusMessage.c_str(), true);
  }

  GUI.drawButtonHints(renderer, tr(STR_BACK), fork_tr(STR_BTN_START), "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSPracticeModeActivity::renderDomainSelect() {
  renderer.clearScreen();

  const int margin = 20;
  const int lineHeight = 35;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  // Title
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_AWS_SELECT_DOMAIN), true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

  renderer.drawLine(margin, y, width - margin, y);
  y += 20;

  // Ensure selected item is visible
  const int visibleItems = (height - y - 60) / lineHeight;
  if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  } else if (selectedIndex >= scrollOffset + visibleItems) {
    scrollOffset = selectedIndex - visibleItems + 1;
  }

  if (domains.empty()) {
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, fork_tr(STR_AWS_NO_DOMAINS), true);
  } else {
    // Draw domains
    for (int i = scrollOffset; i < (int)domains.size() && y < height - 60; i++) {
      bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.fillRect(margin, y, width - 2 * margin, lineHeight);
      }

      int textY = y + (lineHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, margin + 10, textY, domains[i].c_str(), !isSelected);
      y += lineHeight;
    }
  }

  GUI.drawButtonHints(renderer, tr(STR_BACK), fork_tr(STR_BTN_START), "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSPracticeModeActivity::loadDomains() {
  domains.clear();

  char path[128];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());
  HalFile file;
  if (!Storage.openFileForRead("AWS", path, file)) {
    return;
  }

  // Locate the "questions" array using the same scanning approach as AWSCertQuizActivity
  bool foundArray = false;
  char searchBuf[32] = {0};
  int searchPos = 0;

  while (file.available() && !foundArray) {
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
        if (c == '[') {
          foundArray = true;
          break;
        }
        if (c == '{' || c == '}') break;
      }
    }
  }

  if (!foundArray) {
    file.close();
    return;
  }

  // Stream-parse each question object with ArduinoJson to extract "domain" field
  static constexpr size_t DOMAIN_BUF_SIZE = 1536;
  char* buffer = (char*)malloc(DOMAIN_BUF_SIZE);
  if (!buffer) {
    file.close();
    return;
  }

  int bufferPos = 0;
  int braceDepth = 0;
  bool bufferTruncated = false;

  while (file.available()) {
    char c = file.read();

    if (c == '{') {
      if (braceDepth == 0) {
        bufferPos = 0;
        bufferTruncated = false;
      }
      braceDepth++;
    }

    if (braceDepth > 0) {
      if (bufferPos < (int)DOMAIN_BUF_SIZE - 1) {
        buffer[bufferPos++] = c;
      } else {
        bufferTruncated = true;  // Object too large — skip parsing this entry
      }
    }

    if (c == '}') {
      braceDepth--;
      if (braceDepth == 0 && bufferPos > 10 && !bufferTruncated) {
        buffer[bufferPos] = '\0';
        JsonDocument doc;
        if (!deserializeJson(doc, buffer)) {
          const char* domain = doc["domain"] | "General";
          // Case-insensitive dedup: keep first-seen casing, skip if same name in different case
          bool found = false;
          for (const auto& d : domains) {
            if (strcasecmp(d.c_str(), domain) == 0) {
              found = true;
              break;
            }
          }
          if (!found) domains.push_back(String(domain));
        }
      }
    }

    if (c == ']' && braceDepth == 0) break;
  }

  free(buffer);
  file.close();

  std::sort(domains.begin(), domains.end());
  // Prepend "All Domains" so user can practice across all domains without going back
  domains.insert(domains.begin(), String("All Domains"));
}

void AWSPracticeModeActivity::loop() {
  if (menuState == MODE_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onBack();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedIndex > 0) {
        selectedIndex--;
        render();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (selectedIndex < modes.size() - 1) {
        selectedIndex++;
        render();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      const char* modeId = modes[selectedIndex].id;

      if (strcmp(modeId, "domain") == 0) {
        // Load domains and show domain selection
        loadDomains();
        if (domains.size() > 0) {
          savedModeIndex = selectedIndex;
          menuState = DOMAIN_SELECT;
          selectedIndex = 0;
          scrollOffset = 0;
          renderDomainSelect();
        } else {
          // No domains found - show status message and start with "all"
          onSelectMode(modeId, "all");
        }
      } else {
        // Start quiz with selected mode
        onSelectMode(modeId, "all");
      }
    }
  } else if (menuState == DOMAIN_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      menuState = MODE_SELECT;
      selectedIndex = savedModeIndex;
      scrollOffset = 0;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedIndex > 0) {
        selectedIndex--;
        renderDomainSelect();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (selectedIndex < domains.size() - 1) {
        selectedIndex++;
        renderDomainSelect();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // "All Domains" entry maps to the "all" sentinel understood by AWSCertQuizActivity
      const char* domainArg = (domains[selectedIndex] == "All Domains") ? "all" : domains[selectedIndex].c_str();
      onSelectMode("domain", domainArg);
    }
  }
}
