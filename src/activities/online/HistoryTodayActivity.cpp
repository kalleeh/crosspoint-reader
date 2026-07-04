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
#include <I18n.h>
#include <ForkI18n.h>
#include "OnlineContentFetcher.h"
#include "components/UITheme.h"

void HistoryTodayActivity::onEnter() {
  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi() && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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

  // Sync time via NTP so we have the correct calendar date
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  int timeAttempts = 0;
  while (time(nullptr) < 1000000000L && timeAttempts < 20) {
    delay(100);
    timeAttempts++;
  }

  if (time(nullptr) < 1000000000L) {
    // NTP sync failed — cannot determine today's date
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
  http.useHTTP10(true);  // getStreamPtr() can't decode chunked encoding
  char url[128];
  snprintf(url, sizeof(url), "https://en.wikipedia.org/api/rest_v1/feed/onthisday/events/%d/%d", month, day);
  http.begin(url);
  http.setTimeout(15000);  // 15 second timeout for SSL
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  
  DEBUG_PRINTF("[History] HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    // Stream the ~890KB response and filter to just events[].year/text —
    // without the filter deserializeJson materializes the entire parse
    // tree in RAM, which cannot fit on this device.
    WiFiClient* stream = http.getStreamPtr();

    JsonDocument filter;
    filter["events"][0]["year"] = true;
    filter["events"][0]["text"] = true;

    JsonDocument doc;
    DeserializationError error =
        deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
    
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
    const char* msg = fork_tr(STR_ONLINE_LOADING);
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else if (state == ERROR) {
    const char* msg = fork_tr(STR_ONLINE_FAILED_LOAD);
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else {
    int y = margin - scrollOffset;
    
    // Title with date - centered
    String title = String(fork_tr(STR_HISTORY_TITLE));
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
        int breakPos;
        if (renderer.getTextWidth(UI_10_FONT_ID, remaining.c_str()) <= maxWidth) {
          breakPos = remaining.length();
        } else {
          int lo = 0, hi = (int)remaining.length() - 1;
          while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            String test = remaining.substring(0, mid);
            if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) <= maxWidth) {
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
  
  GUI.drawButtonHints(renderer, tr(STR_BACK), state == LOADING ? "" : fork_tr(STR_ONLINE_REFRESH), "", "");
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
