#include "../../DebugConfig.h"
#include "WordOfTheDayActivity.h"
#include <WiFi.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"
#include <I18n.h>
#include <ForkI18n.h>
#include "components/UITheme.h"

void WordOfTheDayActivity::onEnter() {
  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi() && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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
    const char* msg = fork_tr(STR_ONLINE_LOADING);
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else if (state == ERROR) {
    const char* msg = fork_tr(STR_ONLINE_FAILED_LOAD);
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
      int breakPos;
      if (renderer.getTextWidth(UI_10_FONT_ID, remaining.c_str()) <= contentWidth) {
        breakPos = remaining.length();
      } else {
        int lo = 0, hi = (int)remaining.length() - 1;
        while (lo < hi) {
          int mid = lo + (hi - lo + 1) / 2;
          String test = remaining.substring(0, mid);
          if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) <= contentWidth) {
            lo = mid;
          } else {
            hi = mid - 1;
          }
        }
        breakPos = lo;
      }

      if (breakPos < remaining.length()) {
        int lastSpace = remaining.lastIndexOf(' ', breakPos);
        if (lastSpace > 0) breakPos = lastSpace;
      }

      if (breakPos == 0) {
        uint8_t firstByte = (uint8_t)remaining[0];
        if      (firstByte < 0x80) breakPos = 1;
        else if (firstByte < 0xE0) breakPos = 2;
        else if (firstByte < 0xF0) breakPos = 3;
        else                        breakPos = 4;
        if (breakPos > (int)remaining.length()) breakPos = remaining.length();
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
        int breakPos;
        if (renderer.getTextWidth(UI_10_FONT_ID, remaining.c_str()) <= contentWidth) {
          breakPos = remaining.length();
        } else {
          int lo = 0, hi = (int)remaining.length() - 1;
          while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            String test = remaining.substring(0, mid);
            if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) <= contentWidth) {
              lo = mid;
            } else {
              hi = mid - 1;
            }
          }
          breakPos = lo;
        }

        if (breakPos < remaining.length()) {
          int lastSpace = remaining.lastIndexOf(' ', breakPos);
          if (lastSpace > 0) breakPos = lastSpace;
        }

        if (breakPos == 0) {
          uint8_t firstByte = (uint8_t)remaining[0];
          if      (firstByte < 0x80) breakPos = 1;
          else if (firstByte < 0xE0) breakPos = 2;
          else if (firstByte < 0xF0) breakPos = 3;
          else                        breakPos = 4;
          if (breakPos > (int)remaining.length()) breakPos = remaining.length();
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
  
  GUI.drawButtonHints(renderer, tr(STR_BACK), state == LOADING ? "" : fork_tr(STR_ONLINE_REFRESH), "", "");
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
