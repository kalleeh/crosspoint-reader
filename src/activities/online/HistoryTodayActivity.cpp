#include "../../DebugConfig.h"
#include "HistoryTodayActivity.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"

void HistoryTodayActivity::onEnter() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Auto-reconnect to saved network
  
  // Wait up to 5 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(100);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchEvents();
  } else {
    state = ERROR;
    render();
  }
}

void HistoryTodayActivity::onExit() {
  WiFi.mode(WIFI_OFF);
}

void HistoryTodayActivity::fetchEvents() {
  state = LOADING;
  render();
  
  if (WiFi.status() != WL_CONNECTED) {
    state = ERROR;
    render();
    return;
  }
  
  // Get current date
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  
  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%d/%d", month, day);
  date = String(dateStr);
  
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "https://en.wikipedia.org/api/rest_v1/feed/onthisday/events/%d/%d", month, day);
  http.begin(url);
  http.setTimeout(15000);  // 15 second timeout for SSL
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  
  DEBUG_PRINTF("[History] HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    // Use stream to avoid loading entire 890KB response into RAM
    WiFiClient* stream = http.getStreamPtr();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, *stream);
    
    DEBUG_PRINTF("[History] JSON parse: %s\n", error.c_str());
    
    if (error == DeserializationError::Ok) {
      JsonArray eventsArray = doc["events"];
      events.clear();
      
      DEBUG_PRINTF("[History] Events array size: %d\n", eventsArray.size());
      
      if (eventsArray.size() > 0) {
        // Get first 5 events
        int count = 0;
        for (JsonObject event : eventsArray) {
          if (count >= 5) break;
          
          int year = event["year"].as<int>();
          String text = event["text"].as<String>();
          
          DEBUG_PRINTF("[History] Event %d: %d - %s\n", count, year, text.substring(0, 50).c_str());
          
          String eventStr = String(year) + ": " + text;
          events.push_back(eventStr);
          count++;
        }
        
        state = LOADED;
        scrollOffset = 0;
      } else {
        DEBUG_PRINTLN("[History] Events array is empty");
        state = ERROR;
      }
    } else {
      DEBUG_PRINTLN("[History] JSON parse failed");
      state = ERROR;
    }
  } else {
    DEBUG_PRINTF("[History] HTTP failed: %d\n", httpCode);
    state = ERROR;
  }
  
  http.end();
  render();
}

void HistoryTodayActivity::render() {
  renderer.clearScreen();
  
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int margin = 20;
  
  if (state == LOADING) {
    const char* msg = "Loading...";
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else if (state == ERROR) {
    const char* msg = "Failed to load events";
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else {
    int y = margin - scrollOffset;
    
    // Title with date - centered
    String title = "On This Day";
    int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str());
    renderer.drawText(UI_12_FONT_ID, (width - titleWidth) / 2, y, title.c_str(), true);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 5;
    
    // Date - centered
    int dateWidth = renderer.getTextWidth(UI_10_FONT_ID, date.c_str());
    renderer.drawText(UI_10_FONT_ID, (width - dateWidth) / 2, y, date.c_str(), true);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 20;
    
    // Events with bullet points
    const int maxWidth = width - 2 * margin - 15;  // Space for bullet
    
    for (const String& event : events) {
      // Draw bullet
      if (y >= 0 && y < height) {
        renderer.drawText(UI_10_FONT_ID, margin, y, "•", true);
      }
      
      String remaining = event;
      
      while (remaining.length() > 0 && y < height + scrollOffset) {
        int breakPos = remaining.length();
        
        for (int i = 1; i <= remaining.length(); i++) {
          String test = remaining.substring(0, i);
          if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) > maxWidth) {
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
          int xPos = margin + 15;
          renderer.drawText(UI_10_FONT_ID, xPos, y, line.c_str(), true);
        }
        
        y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }
      
      y += 12; // Space between events
    }
    
    maxScroll = max(0, y - height + margin);
  }
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void HistoryTodayActivity::loop() {
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
    fetchEvents();
  }
}
