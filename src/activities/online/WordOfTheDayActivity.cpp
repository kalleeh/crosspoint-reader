#include "../../DebugConfig.h"
#include "WordOfTheDayActivity.h"
#include <WiFi.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"

void WordOfTheDayActivity::onEnter() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Auto-reconnect to saved network
  
  // Wait briefly for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(100);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchWord();
  } else {
    state = ERROR;
    render();
  }
}

void WordOfTheDayActivity::onExit() {
  WiFi.mode(WIFI_OFF);
}

void WordOfTheDayActivity::fetchWord() {
  state = LOADING;
  render();
  
  if (WiFi.status() != WL_CONNECTED) {
    state = ERROR;
    render();
    return;
  }
  
  auto data = OnlineContentFetcher::fetchWordOfDay();
  
  if (data.success) {
    word = data.word.c_str();
    definition = data.definition.c_str();
    example = data.example.c_str();
    state = LOADED;
    scrollOffset = 0;
  } else {
    state = ERROR;
  }
  
  render();
}

void WordOfTheDayActivity::render() {
  renderer.clearScreen();
  
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  
  if (state == LOADING) {
    const char* msg = "Loading...";
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else if (state == ERROR) {
    const char* msg = "Failed to load word";
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else {
    // Zen centered layout
    const int contentWidth = width - 80;  // 40px margin on each side
    int y = 100 - scrollOffset;  // Start lower for centered feel
    
    // Word - centered and prominent
    int wordWidth = renderer.getTextWidth(UI_12_FONT_ID, word.c_str());
    renderer.drawText(UI_12_FONT_ID, (width - wordWidth) / 2, y, word.c_str(), true);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 30;
    
    // Definition - centered with wrapping
    String remaining = definition;
    
    while (remaining.length() > 0 && y < height + scrollOffset) {
      int breakPos = remaining.length();
      
      for (int i = 1; i <= remaining.length(); i++) {
        String test = remaining.substring(0, i);
        if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) > contentWidth) {
          breakPos = i - 1;
          break;
        }
      }
      
      if (breakPos < remaining.length()) {
        int lastSpace = remaining.lastIndexOf(' ', breakPos);
        if (lastSpace > 0) breakPos = lastSpace;
      }
      
      String line = remaining.substring(0, breakPos);
      if (y >= 0 && y < height) {
        int lineWidth = renderer.getTextWidth(UI_10_FONT_ID, line.c_str());
        renderer.drawText(UI_10_FONT_ID, (width - lineWidth) / 2, y, line.c_str(), true);
      }
      
      y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
      remaining = remaining.substring(breakPos);
      remaining.trim();
    }
    
    // Example - centered, with spacing
    if (example.length() > 0) {
      y += 15;  // Extra spacing before example
      
      remaining = example;
      while (remaining.length() > 0 && y < height + scrollOffset) {
        int breakPos = remaining.length();
        
        for (int i = 1; i <= remaining.length(); i++) {
          String test = remaining.substring(0, i);
          if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) > contentWidth) {
            breakPos = i - 1;
            break;
          }
        }
        
        if (breakPos < remaining.length()) {
          int lastSpace = remaining.lastIndexOf(' ', breakPos);
          if (lastSpace > 0) breakPos = lastSpace;
        }
        
        String line = remaining.substring(0, breakPos);
        if (y >= 0 && y < height) {
          int lineWidth = renderer.getTextWidth(UI_10_FONT_ID, line.c_str());
          renderer.drawText(UI_10_FONT_ID, (width - lineWidth) / 2, y, line.c_str(), true);
        }
        
        y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }
    }
    
    maxScroll = max(0, y - height + 40);
  }
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WordOfTheDayActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (scrollOffset > 0) {
      scrollOffset = max(0, scrollOffset - 50);
      render();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (scrollOffset < maxScroll) {
      scrollOffset = min(maxScroll, scrollOffset + 50);
      render();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    fetchWord();
  }
}
