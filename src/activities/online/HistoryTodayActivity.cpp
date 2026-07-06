#include "../../DebugConfig.h"
#include "HistoryTodayActivity.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include <I18n.h>
#include <ForkI18n.h>
#include "OnlineContentFetcher.h"
#include "components/UITheme.h"

namespace {
// HalFile derives from Print, not Stream, so it doesn't match ArduinoJson's
// ArduinoStreamReader specialization. This thin wrapper satisfies the
// generic Reader<TSource> template instead (needs read() -> int and
// readBytes(char*, size_t) -> size_t), letting deserializeJson read directly
// off SD without loading the whole file into a RAM buffer first.
struct HalFileReader {
  explicit HalFileReader(HalFile& file) : file_(file) {}
  int read() { return file_.read(); }
  size_t readBytes(char* buffer, size_t length) {
    int n = file_.read(buffer, length);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
  HalFile& file_;
};
}  // namespace

void HistoryTodayActivity::onEnter() {
  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi(&mappedInput) && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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
    ERROR_PRINTF("[History] NTP sync failed, time()=%ld\n", (long)time(nullptr));
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
  
  char url[128];
  snprintf(url, sizeof(url), "https://en.wikipedia.org/api/rest_v1/feed/onthisday/events/%d/%d", month, day);

  // Download to SD first instead of streaming straight into deserializeJson.
  // NetworkClientSecure::available()/read() call stop() on any transient
  // mbedTLS hiccup mid-transfer (not just real connection loss), which
  // permanently kills the stream — ArduinoJson then sees early EOF and
  // reports IncompleteInput no matter how generous the timeout is (confirmed:
  // this reproduced at both 6s and 15s timeouts, with the whole ~500KB
  // response otherwise fetching in under a second on a healthy connection).
  // XKCDViewerActivity hit the identical stall pattern downloading images;
  // the fix there — and here — is to retry the download itself rather than
  // trying to make a single stream survive the whole transfer.
  const char* tempPath = "/.crosspoint/history_temp.json";
  auto attemptDownload = [&](HTTPClient& httpClient) -> bool {
    httpClient.useHTTP10(true);  // getStreamPtr() can't decode chunked encoding
    httpClient.begin(url);
    httpClient.setReuse(false);
    httpClient.setConnectTimeout(4000);
    httpClient.setTimeout(8000);
    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = httpClient.GET();
    ERROR_PRINTF("[History] GET -> %d\n", httpCode);
    if (httpCode != 200) {
      httpClient.end();
      return false;
    }

    HalFile file;
    if (!Storage.openFileForWrite("HIST", tempPath, file)) {
      httpClient.end();
      return false;
    }

    int contentLength = httpClient.getSize();
    int downloaded = 0;
    bool writeFailed = false;
    bool stalled = false;
    WiFiClient* stream = httpClient.getStreamPtr();
    uint8_t buffer[512];
    unsigned long downloadStart = millis();
    unsigned long lastByteTime = millis();
    while (httpClient.connected() && (contentLength < 0 || downloaded < contentLength)) {
      if (millis() - downloadStart > 20000) break;                     // 20-second wall-clock cap
      if (millis() - lastByteTime > 4000) { stalled = true; break; }    // stream went quiet
      int avail = stream->available();
      if (avail > 0) {
        if (avail > (int)sizeof(buffer)) avail = sizeof(buffer);
        int len = stream->readBytes(buffer, avail);
        if (len > 0) {
          if (file.write(buffer, len) != (size_t)len) {
            writeFailed = true;
            break;
          }
          downloaded += len;
          lastByteTime = millis();
        }
      } else {
        delay(1);
      }
    }
    file.close();
    httpClient.end();
    ERROR_PRINTF("[History] downloaded %d of %d bytes, writeFailed=%d stalled=%d\n", downloaded, contentLength,
                 writeFailed, stalled);
    return !writeFailed && (contentLength < 0 || downloaded >= contentLength);
  };

  HTTPClient http;
  bool ok = attemptDownload(http);
  if (!ok) {
    ERROR_PRINTLN("[History] retrying download with a fresh connection");
    Storage.remove(tempPath);
    HTTPClient retryHttp;
    ok = attemptDownload(retryHttp);
  }

  if (!ok) {
    Storage.remove(tempPath);
    state = ERROR;
    render();
    return;
  }

  // Parse from the now-complete local file — filtered to just
  // events[].year/text, since without the filter deserializeJson
  // materializes the entire parse tree in RAM, which cannot fit on this
  // device.
  HalFile jsonFile;
  if (!Storage.openFileForRead("HIST", tempPath, jsonFile)) {
    Storage.remove(tempPath);
    state = ERROR;
    render();
    return;
  }

  JsonDocument filter;
  filter["events"][0]["year"] = true;
  filter["events"][0]["text"] = true;

  JsonDocument doc;
  HalFileReader reader(jsonFile);
  DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  jsonFile.close();
  Storage.remove(tempPath);

  ERROR_PRINTF("[History] JSON parse: %s\n", error.c_str());

  if (error == DeserializationError::Ok) {
    JsonArray eventsArray = doc["events"];
    events.clear();

    if (eventsArray.size() > 0) {
      // Get first 5 events
      int count = 0;
      for (JsonObject event : eventsArray) {
        if (count >= 5) break;

        int year = event["year"].as<int>();
        String text = event["text"].as<String>();

        String eventStr = String(year) + ": " + text;
        events.push_back(eventStr);
        count++;
      }

      state = LOADED;
      scrollOffset = 0;
    } else {
      ERROR_PRINTLN("[History] Events array is empty");
      state = ERROR;
    }
  } else {
    ERROR_PRINTLN("[History] JSON parse failed");
    state = ERROR;
  }

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
