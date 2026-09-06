#include "components/UITheme.h"
#include "../../DebugConfig.h"
#include "XKCDViewerActivity.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Bitmap.h>
#include <PngToBmpConverter.h>
#include <Memory.h>
#include <algorithm>
#include <cstring>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include <I18n.h>
#include <ForkI18n.h>
#include "OnlineContentFetcher.h"

void XKCDViewerActivity::onEnter() {
  Activity::onEnter();
  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi(&mappedInput) && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchComic(); // Fetch latest
  } else {
    state = ERROR;
    render();
  }
}

void XKCDViewerActivity::onExit() {
  Activity::onExit();
  Storage.remove("/.crosspoint/xkcd_temp.bmp");
  WiFi.mode(WIFI_OFF);
  // No accelerometer on this hardware, so orientation is chosen per-comic
  // (see applyOrientationForImage) rather than sensor-driven; restore
  // portrait for the rest of the UI, matching EpubReaderActivity's pattern.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

void XKCDViewerActivity::applyOrientationForImage(const char* pngPath) {
  // Peek just the IHDR chunk (first 24 bytes) to get the image's pixel
  // dimensions without a full decode, then pick whichever orientation
  // (portrait or landscape) lets the comic render largest on screen —
  // this hardware has no accelerometer, so there's no sensor to drive a
  // real auto-rotate; this is the closest useful equivalent for a single
  // image viewer, similar to how photo viewers orient by image aspect ratio.
  HalFile file;
  if (!Storage.openFileForRead("XKCD", pngPath, file)) return;

  uint8_t header[24];
  bool ok = file.read(header, sizeof(header)) == (int)sizeof(header);
  file.close();
  if (!ok) return;

  static const uint8_t kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (memcmp(header, kPngSignature, 8) != 0 || memcmp(header + 12, "IHDR", 4) != 0) return;

  uint32_t imgWidth, imgHeight;
  memcpy(&imgWidth, header + 16, 4);
  memcpy(&imgHeight, header + 20, 4);
  imgWidth = __builtin_bswap32(imgWidth);
  imgHeight = __builtin_bswap32(imgHeight);
  if (imgWidth == 0 || imgHeight == 0) return;

  // Compare how large the image could render (limited by the tighter of
  // width/height fit) in each orientation; pick whichever allows a bigger
  // scale factor. Portrait is native for this UI (title/alt text stack
  // vertically), so only switch to landscape when it's a clear win.
  const int panelW = renderer.getScreenWidth();   // native portrait width
  const int panelH = renderer.getScreenHeight();  // native portrait height
  constexpr int kChromeMargin = 100;  // approximate space used by title/hints in either orientation

  const float portraitScale =
      std::min(static_cast<float>(panelW - 40) / imgWidth, static_cast<float>(panelH - kChromeMargin) / imgHeight);
  const float landscapeScale =
      std::min(static_cast<float>(panelH - 40) / imgWidth, static_cast<float>(panelW - kChromeMargin) / imgHeight);

  if (landscapeScale > portraitScale * 1.15f) {
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
  } else {
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
}

void XKCDViewerActivity::fetchComic(int num) {
  if (num > 0) currentComic = num;  // update immediately so navigation advances past failed comics
  state = LOADING;
  render();

  // ensureWiFi() is a no-op if already connected, so this only reconnects
  // when needed (e.g. the very first fetch, or after a prior failure).
  if (!OnlineContentFetcher::ensureWiFi(&mappedInput) || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state = ERROR;
    render();
    return;
  }

  HTTPClient http;
  String url = num == 0 ? "https://xkcd.com/info.0.json" : "https://xkcd.com/" + String(num) + "/info.0.json";
  http.begin(url);
  // HTTPClient defaults to _reuse=true, which makes disconnect()/end() skip
  // closing the socket ("kept open for reuse") whenever the server sends
  // Connection: keep-alive (xkcd's does). Since a fresh HTTPClient is created
  // per fetch here rather than actually reused, that socket then just leaks —
  // ESP32-C3's lwIP has a small fixed socket pool, and it was silently
  // exhausted after ~3 comics' worth of leaked sockets (2 per comic: this
  // JSON fetch + the image fetch), after which new connections stalled and
  // timed out (HTTPC_ERROR_READ_TIMEOUT) instead of failing fast.
  http.setReuse(false);
  http.setConnectTimeout(4000);
  http.setTimeout(6000);  // was 10s — a slow-server GET() has zero polling inside it
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  bool haveComic = false;
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;

    if (deserializeJson(doc, payload) == DeserializationError::Ok &&
        !doc["num"].isNull() &&
        !doc["img"].isNull()) {
      currentComic = doc["num"].as<int>();
      title = doc["title"].as<String>();
      alt = doc["alt"].as<String>();
      imageUrl = doc["img"].as<String>();
      // xkcd's API returns an https:// image URL, but imgs.xkcd.com serves
      // identical content over plain HTTP too. Fetching over HTTP avoids
      // opening a second TLS session (mbedTLS handshake buffers, ~35-45KB)
      // for the image download — the actual resource pressure that was
      // causing this second connection to fail outright on this device.
      if (imageUrl.startsWith("https://")) {
        imageUrl = "http://" + imageUrl.substring(8);
      }

      if (num == 0) {
        maxComic = currentComic;
      }

      state = LOADED;
      scrollOffset = 0;
      imageLoaded = false;
      haveComic = true;
    } else {
      state = ERROR;
    }
  } else {
    state = ERROR;
  }

  // Release this connection's TLS session (WiFiClientSecure/mbedTLS buffers,
  // ~35-45KB) and the JSON payload/doc (out of scope now) BEFORE opening a
  // second HTTPS connection for the image. Holding both TLS sessions live at
  // once is what pushed free heap to a near-zero minimum and made the image
  // download's own allocations fail.
  http.end();

  if (haveComic) {
    downloadAndDisplayImage();
  } else if (state == ERROR) {
    render();
  }
}

void XKCDViewerActivity::downloadAndDisplayImage() {
  // Orientation for this comic isn't known yet (needs the downloaded PNG's
  // dimensions); reset to portrait now so error/loading screens below use
  // consistent, known dimensions. applyOrientationForImage() may switch it
  // again once the image is on disk.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  int width = renderer.getScreenWidth();
  int height = renderer.getScreenHeight();
  const int margin = 20;

  // Comic number and title at top
  String header = "#" + String(currentComic) + ": " + title;
  renderer.drawText(UI_12_FONT_ID, margin, margin, header.c_str(), true);

  // Download PNG to temporary file
  HTTPClient http;
  http.begin(imageUrl);
  http.setReuse(false);  // see setReuse comment in fetchComic() above
  http.setConnectTimeout(4000);
  http.setTimeout(8000);  // was 15s — a slow-server GET() has zero polling inside it
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  ERROR_PRINTF("[XKCD] image GET %s -> %d (RSSI=%d)\n", imageUrl.c_str(), httpCode, WiFi.RSSI());
  if (httpCode != 200) {
    http.end();
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_RANDOM),
                                              currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                                              currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // Save to SD card temporarily
  HalFile file;
  if (!Storage.openFileForWrite("XKCD", "/.crosspoint/xkcd_temp.png", file)) {
    http.end();
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // The ESP32 TLS stream has been observed to stall mid-transfer (stream
  // stops delivering bytes with no error, http.connected() stays true) —
  // reproducible on some images, unrelated to heap. A fresh connection
  // reliably recovers, so detect a stall (no bytes for 4s, checked
  // independently of the overall wall-clock cap below) and retry the whole
  // GET once with a brand new HTTPClient/socket before giving up.
  auto attemptDownload = [&](HTTPClient& httpClient) -> bool {
    int contentLength = httpClient.getSize();
    int downloaded = 0;
    bool writeFailed = false;
    bool stalled = false;
    WiFiClient* stream = httpClient.getStreamPtr();
    uint8_t buffer[512];
    unsigned long downloadStart = millis();
    unsigned long lastByteTime = millis();
    while (httpClient.connected() && (contentLength < 0 || downloaded < contentLength)) {
      if (millis() - downloadStart > 20000) break;       // 20-second wall-clock cap
      if (millis() - lastByteTime > 4000) { stalled = true; break; }  // stream went quiet
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
    ERROR_PRINTF("[XKCD] downloaded %d of %d bytes, writeFailed=%d stalled=%d\n", downloaded, contentLength,
                 writeFailed, stalled);
    return !writeFailed && (contentLength < 0 || downloaded >= contentLength);
  };

  bool ok = attemptDownload(http);
  file.close();
  http.end();

  if (!ok) {
    // A retry opens a second TLS session (NetworkClientSecure), which
    // Arduino's HTTPClient allocates with bare `new` — not the project's
    // makeUniqueNoThrow convention. With -fno-exceptions, an OOM there
    // throws std::bad_alloc with nothing to catch it, which calls abort()
    // and reboots the device (confirmed on-device: crashed inside
    // TLSTraits::create() during a low-heap retry). Only retry when there's
    // comfortably enough headroom for that allocation; otherwise fail
    // cleanly instead of risking a crash.
    constexpr size_t kMinHeapForRetry = 60000;
    if (ESP.getFreeHeap() < kMinHeapForRetry) {
      ERROR_PRINTF("[XKCD] skipping retry, only %u bytes free (need ~%u)\n", (unsigned)ESP.getFreeHeap(),
                   (unsigned)kMinHeapForRetry);
    } else {
      ERROR_PRINTLN("[XKCD] retrying image download with a fresh connection");
      Storage.remove("/.crosspoint/xkcd_temp.png");
      if (!Storage.openFileForWrite("XKCD", "/.crosspoint/xkcd_temp.png", file)) {
        renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        return;
      }
      HTTPClient retryHttp;
      retryHttp.begin(imageUrl);
      retryHttp.setReuse(false);  // see setReuse comment in fetchComic() above
      retryHttp.setConnectTimeout(4000);
      retryHttp.setTimeout(8000);
      retryHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      int retryCode = retryHttp.GET();
      ERROR_PRINTF("[XKCD] retry GET -> %d\n", retryCode);
      if (retryCode == 200) {
        ok = attemptDownload(retryHttp);
      }
      file.close();
      retryHttp.end();
    }
  }

  if (!ok) {
    Storage.remove("/.crosspoint/xkcd_temp.png");
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_RANDOM),
                                              currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                                              currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // Pick portrait vs. landscape based on this comic's own aspect ratio (no
  // accelerometer on this hardware, so this is chosen per-image rather than
  // sensor-driven), then re-render the header now that width/height may have
  // swapped.
  applyOrientationForImage("/.crosspoint/xkcd_temp.png");
  width = renderer.getScreenWidth();
  height = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, margin, margin, header.c_str(), true);

  // Show "Loading..." before conversion — PNG decode + BMP re-encode can
  // take a couple seconds on complex images
  renderer.drawText(UI_10_FONT_ID, margin, margin + renderer.getLineHeight(UI_12_FONT_ID) + 20,
                    fork_tr(STR_ONLINE_LOADING), true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  // PNGdec needs one ~58-59KB contiguous heap block to hold its whole-image
  // decode state, which this device's allocator can no longer provide after
  // a single HTTPS/TLS round trip has fragmented the free list. PngToBmpConverter
  // streams instead, but its InflateStream still wants ~11KB of tinfl state plus
  // a 32KB DEFLATE window. Lend it the 48KB framebuffer for the duration of the
  // conversion (GfxRenderer::FrameBufferLoan -> buildscratch::claim()) so the
  // decode costs the fragmented heap nothing. The panel keeps showing the
  // "Loading..." screen pushed above; the loan hands the buffer back white and
  // we redraw the whole screen below anyway.
  const int topMargin = 60;
  const int bottomMargin = 40;
  const int availWidth = width - 40;
  const int availHeight = height - topMargin - bottomMargin;

  bool converted = false;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    HalFile pngFile;
    if (Storage.openFileForRead("XKCD", "/.crosspoint/xkcd_temp.png", pngFile)) {
      HalFile bmpFile;
      if (Storage.openFileForWrite("XKCD", "/.crosspoint/xkcd_temp.bmp", bmpFile)) {
        converted = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, bmpFile, availWidth, availHeight, false);
      }
    }
  }
  Storage.remove("/.crosspoint/xkcd_temp.png");
  if (!converted) {
    Storage.remove("/.crosspoint/xkcd_temp.bmp");
    ERROR_PRINTLN("[XKCD] PNG to BMP conversion failed");
  }

  // Redraw cleanly: erase the "Loading..." status line before adding the
  // image (or failure message).
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, margin, margin, header.c_str(), true);

  if (converted) {
    imageLoaded = true;
    drawCachedComicImage();
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_RANDOM),
                                            currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                                            currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Display the complete buffer (title + image + menu)
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XKCDViewerActivity::drawCachedComicImage() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int topMargin = 60;
  const int bottomMargin = 40;
  const int availWidth = screenWidth - 40;
  const int availHeight = screenHeight - topMargin - bottomMargin;

  HalFile bmpFile;
  if (!Storage.openFileForRead("XKCD", "/.crosspoint/xkcd_temp.bmp", bmpFile)) return;

  Bitmap bitmap(bmpFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return;

  const int xOffset = (screenWidth - bitmap.getWidth()) / 2;
  const int yOffset = topMargin + max(0, (availHeight - bitmap.getHeight()) / 2);
  renderer.drawBitmap(bitmap, xOffset, yOffset, availWidth, availHeight);
}

void XKCDViewerActivity::render() {
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

    // Comic number and title
    String header = "#" + String(currentComic) + ": " + title;
    renderer.drawText(UI_12_FONT_ID, margin, y, header.c_str(), true);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 15;

    // Skip space for image if loaded; redraw it from the cached BMP so it
    // survives repeated render() calls (e.g. scrolling), since clearScreen()
    // above wipes any previously-drawn pixels.
    if (imageLoaded) {
      drawCachedComicImage();
      y = height - 150; // Position alt text at bottom
    } else {
      const char* note = fork_tr(STR_ONLINE_IMAGE_LOADING);
      renderer.drawText(UI_10_FONT_ID, margin, y, note, true);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 15;
    }

    // Alt text with wrapping — bumped from UI_10 to UI_12 (matches the
    // title size) since this is the primary reading content in this
    // activity, not a secondary label.
    const int maxWidth = width - 2 * margin;
    String remaining = alt;

    while (remaining.length() > 0 && y < height + scrollOffset) {
      int breakPos;
      if (renderer.getTextWidth(UI_12_FONT_ID, remaining.c_str()) <= maxWidth) {
        breakPos = remaining.length();
      } else {
        int lo = 0, hi = (int)remaining.length() - 1;
        while (lo < hi) {
          int mid = lo + (hi - lo + 1) / 2;
          String test = remaining.substring(0, mid);
          if (renderer.getTextWidth(UI_12_FONT_ID, test.c_str()) <= maxWidth) {
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
        else if (firstByte < 0xE0) breakPos = 2;  // 2-byte lead
        else if (firstByte < 0xF0) breakPos = 3;  // 3-byte lead
        else                        breakPos = 4;  // 4-byte lead
        if (breakPos > (int)remaining.length()) breakPos = remaining.length();
      }

      String line = remaining.substring(0, breakPos);
      if (y >= 0 && y < height) {
        renderer.drawText(UI_12_FONT_ID, margin, y, line.c_str(), true);
      }

      y += renderer.getLineHeight(UI_12_FONT_ID) + 2;
      remaining = remaining.substring(breakPos);
      remaining.trim();
    }

    y += 20;

    // Navigation hint
    const char* nav = fork_tr(STR_XKCD_NAV);
    if (y >= 0 && y < height) {
      renderer.drawText(UI_10_FONT_ID, margin, y, nav, true);
    }
    y += renderer.getLineHeight(UI_10_FONT_ID);

    maxScroll = max(0, y - height + margin);
  }

  // Button hints stay visible in every state (including LOADING/ERROR) so
  // Back is always labeled, not just once content has loaded. Routed through
  // mapLabels() (not raw drawButtonHints slots) so labels track the user's
  // front-button remapping and orientation, matching the rest of the codebase.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_RANDOM),
                                            currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                                            currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XKCDViewerActivity::loop() {
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
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (currentComic > 1) {
      fetchComic(currentComic - 1);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (currentComic < maxComic) {
      fetchComic(currentComic + 1);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Random comic
    int randomNum = random(1, maxComic + 1);
    fetchComic(randomNum);
  }
}
