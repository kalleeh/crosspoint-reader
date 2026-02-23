#include "../../DebugConfig.h"
#include "WikipediaRandomActivity.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <JpegToBmpConverter.h>
#include <Bitmap.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"

void WikipediaRandomActivity::onEnter() {
  DEBUG_PRINTF("[%lu] [WIKI] Activity entered\n", millis());
  
  // Show empty feed immediately
  render();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  
  DEBUG_PRINTF("[%lu] [WIKI] Waiting for WiFi...\n", millis());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(100);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    DEBUG_PRINTF("[%lu] [WIKI] WiFi connected\n", millis());
    loadInterests();
    sessionStartTime = millis();
    lastFetchTime = millis();  // Initialize to prevent immediate reload
    
    // Fetch articles one by one, rendering after each
    isFetching = true;
    for (int i = 0; i < 5; i++) {
      DEBUG_PRINTF("[%lu] [WIKI] Fetching article %d/5\n", millis(), i + 1);
      
      // Add delay between requests to avoid rate limiting
      if (i > 0) delay(1000);
      
      auto data = OnlineContentFetcher::fetchWikipediaRandom();
      DEBUG_PRINTF("[%lu] [WIKI] Fetch complete, success=%d\n", millis(), data.success);
      
      if (data.success) {
        WikiArticle article;
        article.title = data.title;
        article.extract = data.extract;
        article.imageUrl = data.imageUrl;
        
        DEBUG_PRINTF("[%lu] [WIKI] Article: %s (image: %s)\n", 
                      millis(), article.title.c_str(), 
                      article.imageUrl.isEmpty() ? "none" : "yes");
        
        String title = data.title;
        title.toLowerCase();
        int start = 0;
        for (int j = 0; j <= title.length(); j++) {
          if (j == title.length() || title[j] == ' ' || title[j] == ',' || title[j] == '-') {
            if (j > start) {
              String word = title.substring(start, j);
              if (word.length() > 3) {
                article.categories.push_back(word);
              }
            }
            start = j + 1;
          }
        }
        
        feed.push_back(article);
        DEBUG_PRINTF("[%lu] [WIKI] Added to feed, rendering...\n", millis());
        render();  // Show article immediately (also computes startY/endY)
        evictOldArticles();
        DEBUG_PRINTF("[%lu] [WIKI] Render complete\n", millis());
      }
    }
    isFetching = false;
    
    DEBUG_PRINTF("[%lu] [WIKI] Fetched %d articles, final render...\n", millis(), feed.size());
    render();
    DEBUG_PRINTF("[%lu] [WIKI] Final render complete\n", millis());
  } else {
    DEBUG_PRINTF("[%lu] [WIKI] WiFi failed\n", millis());
    state = ERROR;
    render();
  }
}

void WikipediaRandomActivity::onExit() {
  // Track engagement for all visible articles
  for (auto& article : feed) {
    if (article.scrollDepth > 0) {
      updateInterests(article, article.scrollDepth);
    }
  }
  saveInterests();
  WiFi.mode(WIFI_OFF);
}

void WikipediaRandomActivity::loadInterests() {
  const char* path = "/.crosspoint/wiki_interests.txt";
  FsFile file;
  if (!SdMan.openFileForRead("WIKI", path, file)) return;
  
  char line[128];
  while (file.available()) {
    int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (len <= 0) continue;
    line[len] = '\0';
    
    char* sep = strchr(line, ':');
    if (!sep) continue;
    *sep = '\0';
    
    String keyword = String(line);
    float weight = atof(sep + 1);
    interests[keyword] = weight;
  }
  file.close();
}

void WikipediaRandomActivity::saveInterests() {
  const char* path = "/.crosspoint/wiki_interests.txt";
  FsFile file;
  if (!SdMan.openFileForWrite("WIKI", path, file)) return;
  
  for (const auto& pair : interests) {
    String line = pair.first + ":" + String(pair.second, 2) + "\n";
    file.write((const uint8_t*)line.c_str(), line.length());
  }
  file.close();
}

void WikipediaRandomActivity::updateInterests(const WikiArticle& article, int scrollDepth) {
  if (scrollDepth < 100) return;  // Didn't really read it
  
  float engagement = min(1.0f, scrollDepth / 500.0f);  // Normalize scroll depth
  
  for (const String& cat : article.categories) {
    String lower = cat;
    lower.toLowerCase();
    interests[lower] = min(10.0f, interests[lower] + engagement);
  }
}

float WikipediaRandomActivity::scoreArticle(const WikiArticle& article) {
  if (interests.empty()) return 1.0f;
  
  float score = 0.0f;
  for (const String& cat : article.categories) {
    String lower = cat;
    lower.toLowerCase();
    if (interests.count(lower)) {
      score += interests[lower];
    }
  }
  return score;
}

bool WikipediaRandomActivity::downloadAndCacheImage(WikiArticle& article) {
  if (article.imageUrl.isEmpty()) return false;
  if (!article.cachedImagePath.isEmpty()) return true;
  
  // Create cache path
  uint32_t hash = 0;
  for (int i = 0; i < article.imageUrl.length(); i++) {
    hash = hash * 31 + article.imageUrl[i];
  }
  article.cachedImagePath = "/.crosspoint/wiki_" + String(hash) + ".bmp";
  
  if (SdMan.exists(article.cachedImagePath.c_str())) {
    return true;
  }
  
  // Download image
  HTTPClient http;
  http.begin(article.imageUrl);
  int httpCode = http.GET();
  
  if (httpCode != 200) {
    http.end();
    return false;
  }
  
  // Save to temp
  String tempPath = "/.crosspoint/wiki_temp.jpg";
  FsFile tempFile;
  if (!SdMan.openFileForWrite("WIKI", tempPath.c_str(), tempFile)) {
    http.end();
    return false;
  }
  
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  while (http.connected() && (stream->available() > 0 || http.connected())) {
    int len = stream->readBytes(buffer, sizeof(buffer));
    if (len > 0) {
      tempFile.write(buffer, len);
    }
  }
  tempFile.close();
  http.end();
  
  // Convert to BMP
  if (!SdMan.openFileForRead("WIKI", tempPath.c_str(), tempFile)) {
    return false;
  }
  
  FsFile bmpFile;
  if (!SdMan.openFileForWrite("WIKI", article.cachedImagePath.c_str(), bmpFile)) {
    tempFile.close();
    return false;
  }
  
  bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(tempFile, bmpFile, 480, 400);
  
  tempFile.close();
  bmpFile.close();
  SdMan.remove(tempPath.c_str());
  
  return success;
}

void WikipediaRandomActivity::evictOldArticles() {
  while (static_cast<int>(feed.size()) > MAX_FEED_SIZE && feed.size() > 1) {
    // Track engagement before evicting
    if (feed[0].scrollDepth > 0) {
      updateInterests(feed[0], feed[0].scrollDepth);
    }

    // Adjust scroll so the visible content stays in place.
    // startY values are screen-relative, but the *difference* between two
    // consecutive startY values equals the virtual height of the first article
    // (independent of scrollOffset), so subtracting it keeps the view stable.
    const int heightRemoved = feed[1].startY - feed[0].startY;
    scrollOffset = max(0, scrollOffset - heightRemoved);

    feed.erase(feed.begin());
  }
}

void WikipediaRandomActivity::fetchNextArticles() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (isFetching) return;  // Already fetching
  
  lastFetchTime = millis();
  isFetching = true;
  
  // Fetch 5 articles one by one
  for (int i = 0; i < 5; i++) {
    DEBUG_PRINTF("[%lu] [WIKI] Fetching article %d/5\n", millis(), i + 1);
    
    // Add delay between requests to avoid rate limiting
    if (i > 0) delay(1000);
    
    auto data = OnlineContentFetcher::fetchWikipediaRandom();
    if (data.success) {
      WikiArticle article;
      article.title = data.title;
      article.extract = data.extract;
      article.imageUrl = data.imageUrl;
      
      // Extract keywords from title
      String title = data.title;
      title.toLowerCase();
      int start = 0;
      for (int j = 0; j <= title.length(); j++) {
        if (j == title.length() || title[j] == ' ' || title[j] == ',' || title[j] == '-') {
          if (j > start) {
            String word = title.substring(start, j);
            if (word.length() > 3) {
              article.categories.push_back(word);
            }
          }
          start = j + 1;
        }
      }
      
      feed.push_back(article);
      render();  // Show article immediately (also computes startY/endY)
      evictOldArticles();
    }
  }
  
  isFetching = false;
  DEBUG_PRINTF("[%lu] [WIKI] Feed size: %d articles\n", millis(), feed.size());
  render();
}

void WikipediaRandomActivity::render() {
  renderer.clearScreen();
  
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int margin = 20;
  
  if (state == ERROR) {
    const char* msg = "Failed to load";
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else {
    int y = margin - scrollOffset;
    
    // Render all articles in feed
    for (auto& article : feed) {
      if (y > height) break;  // Off screen
      
      int articleStartY = y;
      
      // Title
      String titleText = article.title;
      const int maxTitleWidth = width - 2 * margin;
      String remaining = titleText;
      
      while (remaining.length() > 0) {
        int breakPos = remaining.length();
        
        for (int i = 1; i <= remaining.length(); i++) {
          String test = remaining.substring(0, i);
          if (renderer.getTextWidth(UI_12_FONT_ID, test.c_str()) > maxTitleWidth) {
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
          renderer.drawText(UI_12_FONT_ID, margin, y, line.c_str(), true);
        }
        
        y += renderer.getLineHeight(UI_12_FONT_ID) + 2;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }
      
      y += 10;
      
      // Image (if available) - only download if cached or not fetching
      if (!article.imageUrl.isEmpty()) {
        bool hasCache = !article.cachedImagePath.isEmpty() && 
                        SdMan.exists(article.cachedImagePath.c_str());
        
        if (y >= -400 && y < height) {  // Render if partially visible
          // Only download if not currently fetching articles (prevents blocking)
          if (hasCache || !isFetching) {
            DEBUG_PRINTF("[WIKI] Downloading image for: %s (cached: %d)\n", 
                          article.title.c_str(), hasCache);
            if (downloadAndCacheImage(article)) {
              DEBUG_PRINTF("[WIKI] Image downloaded, rendering bitmap\n");
              FsFile bmpFile;
              if (SdMan.openFileForRead("WIKI", article.cachedImagePath.c_str(), bmpFile)) {
                Bitmap bitmap(bmpFile);
                renderer.drawBitmap(bitmap, 0, y, width, 400);
                bmpFile.close();
                DEBUG_PRINTF("[WIKI] Bitmap rendered\n");
              }
            } else {
              DEBUG_PRINTF("[WIKI] Image download failed\n");
            }
          }
        }
        y += 410;
      }
      
      // Extract
      const int maxWidth = width - 2 * margin;
      remaining = article.extract;
      
      while (remaining.length() > 0) {
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
          renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str(), true);
        }
        
        y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }
      
      y += 40;  // Space between articles
      
      // Track article position
      article.startY = articleStartY;
      article.endY = y;
      
      // Stop rendering if way off screen
      if (y > height + 1000) break;
      
      // Track scroll depth for this article
      if (scrollOffset >= articleStartY && scrollOffset < y) {
        article.scrollDepth = max(article.scrollDepth, scrollOffset - articleStartY);
      }
    }
    
    maxScroll = max(0, y - height + margin);
    
    // Legend (show loading status)
    const char* btn2 = isFetching ? "" : "Load More";
    renderer.drawButtonHints(UI_10_FONT_ID, "Back", btn2, "", "");
  }
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WikipediaRandomActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    scrollOffset = max(0, scrollOffset - 100);
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    scrollOffset += 100;
    render();
    
    // Find which article is currently visible
    int currentArticleIndex = -1;
    for (int i = 0; i < feed.size(); i++) {
      if (scrollOffset >= feed[i].startY && scrollOffset < feed[i].endY) {
        currentArticleIndex = i;
        break;
      }
    }
    
    // Load more if viewing one of the last 2 articles AND we have WiFi
    unsigned long now = millis();
    if (currentArticleIndex >= (int)feed.size() - 2 && 
        !isFetching &&
        WiFi.status() == WL_CONNECTED &&
        (now - lastFetchTime) > 5000) {
      DEBUG_PRINTF("[WIKI] Auto-load: viewing article %d/%d\n", 
                    currentArticleIndex + 1, feed.size());
      fetchNextArticles();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    fetchNextArticles();
  }
}
