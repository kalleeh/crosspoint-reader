#include "../../DebugConfig.h"
#include "AWSPracticeModeActivity.h"
#include "../../fontIds.h"
#include <HalDisplay.h>
#include <SDCardManager.h>

void AWSPracticeModeActivity::onEnter() {
  render();
}

void AWSPracticeModeActivity::render() {
  renderer.clearScreen();
  
  const int margin = 20;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  
  // Title
  int y = margin;
  renderer.drawText(UI_12_FONT_ID, margin, y, "Select Practice Mode", true);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
  
  renderer.drawLine(margin, y, width - margin, y);
  y += 20;
  
  // Calculate item heights dynamically
  std::vector<int> itemHeights;
  for (const auto& mode : modes) {
    int itemHeight = 10;  // Top padding
    itemHeight += renderer.getLineHeight(UI_10_FONT_ID);  // Name
    itemHeight += 5;  // Spacing
    itemHeight += renderer.getLineHeight(UI_10_FONT_ID);  // Description
    itemHeight += 10;  // Bottom padding
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
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Start", "", "");
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
  renderer.drawText(UI_12_FONT_ID, margin, y, "Select Domain", true);
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
  
  // Draw domains
  for (int i = scrollOffset; i < domains.size() && y < height - 60; i++) {
    bool isSelected = (i == selectedIndex);
    
    if (isSelected) {
      renderer.fillRect(margin, y, width - 2 * margin, lineHeight);
    }
    
    int textY = y + (lineHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, margin + 10, textY, domains[i].c_str(), !isSelected);
    y += lineHeight;
  }
  
  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Start", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AWSPracticeModeActivity::loadDomains() {
  domains.clear();
  
  // Load questions and extract unique domains
  char path[128];
  snprintf(path, sizeof(path), "/aws-quiz/%s.json", certId.c_str());
  FsFile file;
  if (!SdMan.openFileForRead("AWS", path, file)) {
    return;
  }
  
  // Simple scan for domain fields
  String line;
  while (file.available() && domains.size() < 20) {
    char c = file.read();
    line += c;
    
    if (line.indexOf("\"domain\":") >= 0) {
      // Extract domain value
      int start = line.indexOf("\"domain\":") + 10;
      int quoteStart = line.indexOf("\"", start);
      int quoteEnd = line.indexOf("\"", quoteStart + 1);
      
      if (quoteStart >= 0 && quoteEnd > quoteStart) {
        String domain = line.substring(quoteStart + 1, quoteEnd);
        
        // Add if unique
        bool found = false;
        for (const auto& d : domains) {
          if (d == domain) {
            found = true;
            break;
          }
        }
        if (!found) {
          domains.push_back(domain);
        }
      }
      line = "";
    }
    
    if (line.length() > 200) line = "";
  }
  
  file.close();
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
          menuState = DOMAIN_SELECT;
          selectedIndex = 0;
          scrollOffset = 0;
          renderDomainSelect();
        } else {
          // No domains found, start with "all"
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
      selectedIndex = 0;
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
      onSelectMode("domain", domains[selectedIndex].c_str());
    }
  }
}
