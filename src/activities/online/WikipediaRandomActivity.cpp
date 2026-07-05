#include "components/UITheme.h"
#include "../../DebugConfig.h"
#include "WikipediaRandomActivity.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Bitmap.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"
#include <I18n.h>
#include <ForkI18n.h>

void WikipediaRandomActivity::onEnter() {
  DEBUG_PRINTF("[%lu] [WIKI] Activity entered\n", millis());

  pendingFetches = 0;
  isFetching = false;

  // Show empty feed immediately
  render();

  // Connect using CrossPoint's saved WiFi credentials
  DEBUG_PRINTF("[%lu] [WIKI] Waiting for WiFi...\n", millis());
  if (OnlineContentFetcher::ensureWiFi(&mappedInput) && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    DEBUG_PRINTF("[%lu] [WIKI] WiFi connected\n", millis());
    loadInterests();
    lastFetchTime = millis();  // Initialize to prevent immediate reload

    isFetching = true;
    auto data = OnlineContentFetcher::fetchWikipediaRandom(&mappedInput);
    if (data.cancelled) {
      isFetching = false;
      onBack();
      return;
    }
    if (data.success) {
      WikiArticle article;
      article.title = data.title;
      article.extract = data.extract;
      article.imageUrl = data.imageUrl;

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
      evictOldArticles();
    }
    isFetching = false;
    pendingFetches = 4;
    render();
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
  HalFile file;
  if (!Storage.openFileForRead("WIKI", path, file)) return;

  char line[128];
  while (file.available()) {
    // HalFile has no readBytesUntil(); read a line one byte at a time.
    int len = 0;
    int c;
    while (len < static_cast<int>(sizeof(line) - 1) && (c = file.read()) >= 0) {
      if (c == '\n') break;
      line[len++] = static_cast<char>(c);
    }
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
  HalFile file;
  if (!Storage.openFileForWrite("WIKI", path, file)) return;

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

  // Prune to prevent unbounded growth across sessions
  static constexpr int MAX_INTERESTS = 500;
  if ((int)interests.size() > MAX_INTERESTS) {
    auto minIt = interests.begin();
    for (auto it = std::next(interests.begin()); it != interests.end(); ++it) {
      if (it->second < minIt->second) minIt = it;
    }
    interests.erase(minIt);
  }
}

bool WikipediaRandomActivity::downloadAndCacheImage(WikiArticle& article) {
  if (article.imageUrl.isEmpty()) return false;

  // Create cache path as a local candidate; only assigned to article on success
  uint32_t hash = 0;
  for (int i = 0; i < article.imageUrl.length(); i++) {
    hash = hash * 31 + article.imageUrl[i];
  }
  String candidatePath = "/.crosspoint/wiki_" + String(hash) + ".bmp";

  if (Storage.exists(candidatePath.c_str())) {
    article.cachedImagePath = candidatePath;
    return true;
  }

  // Download image
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(8000);  // was 15s — a slow-server GET() has zero polling inside it
  http.begin(article.imageUrl);
  // HTTPClient defaults to _reuse=true, which skips closing the socket on
  // end() whenever the server sends Connection: keep-alive. Since a fresh
  // HTTPClient is created per image here rather than actually reused, that
  // socket then just leaks — confirmed on XKCDViewerActivity that ESP32-C3's
  // small fixed lwIP socket pool gets silently exhausted after a handful of
  // leaked sockets, after which new connections stall and time out.
  http.setReuse(false);
  int httpCode = http.GET();

  if (httpCode != 200) {
    http.end();
    return false;
  }

  // Save to temp
  String tempPath = "/.crosspoint/wiki_temp.jpg";
  HalFile tempFile;
  if (!Storage.openFileForWrite("WIKI", tempPath.c_str(), tempFile)) {
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  int downloaded = 0;
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  unsigned long downloadStart = millis();
  while (http.connected() && (contentLength < 0 || downloaded < contentLength)) {
    if (millis() - downloadStart > 20000) break;  // 20-second wall-clock cap
    if (OnlineContentFetcher::pollCancel(&mappedInput)) {
      // Back mid-download: abort, clean up, and leave the activity
      tempFile.close();
      Storage.remove(tempPath.c_str());
      http.end();
      cancelRequested = true;
      return false;
    }
    int avail = stream->available();
    if (avail > 0) {
      if (avail > (int)sizeof(buffer)) avail = sizeof(buffer);
      int len = stream->readBytes(buffer, avail);
      if (len > 0) {
        tempFile.write(buffer, len);
        downloaded += len;
      }
    } else {
      delay(1);
    }
  }
  tempFile.close();
  http.end();

  // Convert to BMP
  if (!Storage.openFileForRead("WIKI", tempPath.c_str(), tempFile)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  HalFile bmpFile;
  if (!Storage.openFileForWrite("WIKI", candidatePath.c_str(), bmpFile)) {
    tempFile.close();
    return false;
  }

  bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(tempFile, bmpFile, 480, 400);

  tempFile.close();
  bmpFile.close();
  Storage.remove(tempPath.c_str());
  if (!success) Storage.remove(candidatePath.c_str());

  if (success) { article.cachedImagePath = candidatePath; }
  return success;
}

void WikipediaRandomActivity::evictOldArticles() {
  while (static_cast<int>(feed.size()) > MAX_FEED_SIZE && feed.size() > 1) {
    // Track engagement before evicting
    if (feed[0].scrollDepth > 0) {
      updateInterests(feed[0], feed[0].scrollDepth);
    }

    // Adjust scroll so the visible content stays in place.
    // startY values are virtual (document) coordinates, so the difference
    // between consecutive startY values equals the virtual height of the
    // evicted article; subtracting it keeps the view stable.
    const int heightRemoved = feed[1].startY - feed[0].startY;
    scrollOffset = max(0, scrollOffset - heightRemoved);

    if (!feed[0].cachedImagePath.isEmpty()) {
      Storage.remove(feed[0].cachedImagePath.c_str());
    }

    feed.erase(feed.begin());
  }
}

bool WikipediaRandomActivity::downloadNextVisibleImage() {
  for (auto& article : feed) {
    if (!article.imageUrl.isEmpty() && article.cachedImagePath.isEmpty() && !article.imageDownloadFailed) {
      // Skip articles not yet rendered (startY/endY still at default 0)
      if (article.endY == 0) continue;
      // Only download if near the visible area
      if (article.startY < scrollOffset + renderer.getScreenHeight() + 400 &&
          article.endY > scrollOffset - 400) {
        if (downloadAndCacheImage(article)) {
          return true;   // success — caller should re-render
        } else {
          article.imageDownloadFailed = true;
          return false;  // failure marked — caller should not re-render
        }
      }
    }
  }
  return false;
}

void WikipediaRandomActivity::fetchSingleArticle() {
  if (isFetching) return;
  if (WiFi.status() != WL_CONNECTED) return;
  isFetching = true;

  auto data = OnlineContentFetcher::fetchWikipediaRandom(&mappedInput);
  if (data.cancelled) {
    // Back pressed mid-fetch: pollCancel consumed the press event, so the
    // outer loop() won't see it — leave the activity from here.
    isFetching = false;
    pendingFetches = 0;
    onBack();
    return;
  }
  if (data.success) {
    WikiArticle article;
    article.title = data.title;
    article.extract = data.extract;
    article.imageUrl = data.imageUrl;

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
    evictOldArticles();
  }

  isFetching = false;
}

void WikipediaRandomActivity::fetchNextArticles() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (isFetching) return;
  if (pendingFetches < 10) pendingFetches += 5;
  lastFetchTime = millis();
}

void WikipediaRandomActivity::render() {
  renderer.clearScreen();

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int margin = 20;

  if (state == ERROR) {
    const char* msg = fork_tr(STR_ONLINE_FAILED_LOAD);
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else {
    int y = margin - scrollOffset;
    int virtualY = margin;

    // Render all articles in feed
    for (auto& article : feed) {
      int articleStartY = y;
      int virtualArticleStartY = virtualY;

      // Title
      String titleText = article.title;
      const int maxTitleWidth = width - 2 * margin;
      String remaining = titleText;

      while (remaining.length() > 0) {
        int breakPos;
        if (renderer.getTextWidth(UI_12_FONT_ID, remaining.c_str()) <= maxTitleWidth) {
          breakPos = remaining.length();
        } else {
          int lo = 0, hi = (int)remaining.length() - 1;
          while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            String test = remaining.substring(0, mid);
            if (renderer.getTextWidth(UI_12_FONT_ID, test.c_str()) <= maxTitleWidth) {
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
          // Advance past the complete UTF-8 codepoint to avoid splitting multi-byte sequences
          uint8_t firstByte = (uint8_t)remaining[0];
          if      (firstByte < 0x80) breakPos = 1;  // ASCII
          else if (firstByte < 0xE0) breakPos = 2;  // 2-byte lead (0xC0–0xDF)
          else if (firstByte < 0xF0) breakPos = 3;  // 3-byte lead (0xE0–0xEF)
          else                        breakPos = 4;  // 4-byte lead (0xF0–0xF7)
          if (breakPos > (int)remaining.length()) breakPos = remaining.length();
        }

        String line = remaining.substring(0, breakPos);
        if (y >= 0 && y < height) {
          renderer.drawText(UI_12_FONT_ID, margin, y, line.c_str(), true);
        }

        y += renderer.getLineHeight(UI_12_FONT_ID) + 2;
        virtualY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }

      y += 10;
      virtualY += 10;

      // Image (if available) - only download if cached or not fetching
      if (!article.imageUrl.isEmpty()) {
        if (y >= -400 && y < height && !article.cachedImagePath.isEmpty()) {
          HalFile bmpFile;
          if (Storage.openFileForRead("WIKI", article.cachedImagePath.c_str(), bmpFile)) {
            Bitmap bitmap(bmpFile);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              renderer.drawBitmap(bitmap, 0, y, width, 400);
            }
            bmpFile.close();
          }
        }
        y += 410;
        virtualY += 410;
      }

      // Extract
      const int maxWidth = width - 2 * margin;
      remaining = article.extract;

      while (remaining.length() > 0) {
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
          // Advance past the complete UTF-8 codepoint to avoid splitting multi-byte sequences
          uint8_t firstByte = (uint8_t)remaining[0];
          if      (firstByte < 0x80) breakPos = 1;  // ASCII
          else if (firstByte < 0xE0) breakPos = 2;  // 2-byte lead (0xC0–0xDF)
          else if (firstByte < 0xF0) breakPos = 3;  // 3-byte lead (0xE0–0xEF)
          else                        breakPos = 4;  // 4-byte lead (0xF0–0xF7)
          if (breakPos > (int)remaining.length()) breakPos = remaining.length();
        }

        String line = remaining.substring(0, breakPos);
        if (y >= 0 && y < height) {
          renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str(), true);
        }

        y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        virtualY += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        remaining = remaining.substring(breakPos);
        remaining.trim();
      }

      y += 40;  // Space between articles
      virtualY += 40;

      // Track article position
      article.startY = virtualArticleStartY;
      article.endY = virtualY;

      // Stop rendering if way off screen
      if (y > height + 1000) break;

      // Track scroll depth for this article
      if (scrollOffset >= virtualArticleStartY && scrollOffset < virtualY) {
        article.scrollDepth = max(article.scrollDepth, scrollOffset - virtualArticleStartY);
      }
    }

    maxScroll = max(0, virtualY - height);
  }

  // Button hints stay visible in every state (including ERROR) so Back is
  // always labeled, not just once the feed has loaded.
  const char* btn2 =
      (state == ERROR || isFetching || pendingFetches > 0) ? "" : fork_tr(STR_ONLINE_LOAD_MORE);
  GUI.drawButtonHints(renderer, tr(STR_BACK), btn2, "", "");

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WikipediaRandomActivity::loop() {
  if (cancelRequested || mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (pendingFetches > 0 && !isFetching) {
    pendingFetches--;
    fetchSingleArticle();
    render();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    scrollOffset = max(0, scrollOffset - 100);
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    scrollOffset = min(scrollOffset + 100, maxScroll);
    render();
    scrollOffset = min(scrollOffset, maxScroll);

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

  // Download one pending image per loop() call when not fetching articles
  if (pendingFetches == 0 && !isFetching) {
    if (downloadNextVisibleImage()) {
      render();
    }
  }
}
