#include "components/UITheme.h"
#include "../../DebugConfig.h"
#include "XKCDViewerActivity.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include <I18n.h>
#include <ForkI18n.h>
#include <new>
#include "OnlineContentFetcher.h"

// Static pointer for PNG callback
static XKCDViewerActivity* s_instance = nullptr;
static HalFile s_pngFile;

// Floyd-Steinberg dithering buffers
static int16_t* errorBuffer = nullptr;
static int16_t* nextErrorBuffer = nullptr;
static int ditherWidth = 0;
static uint16_t* s_lineBuffer = nullptr;

// PNG file callbacks
void* pngOpen(const char *filename, int32_t *size) {
  if (s_pngFile) s_pngFile.close();
  if (Storage.openFileForRead("XKCD", filename, s_pngFile)) {
    *size = s_pngFile.fileSize();
    return &s_pngFile;
  }
  return nullptr;
}

void pngClose(void *handle) {
  if (s_pngFile) {
    s_pngFile.close();
  }
}

int32_t pngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  return s_pngFile.read(buffer, length);
}

int32_t pngSeek(PNGFILE *handle, int32_t position) {
  return s_pngFile.seek(position);
}

void XKCDViewerActivity::onEnter() {
  s_instance = this;
  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi() && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchComic(); // Fetch latest
  } else {
    state = ERROR;
    render();
  }
}

void XKCDViewerActivity::onExit() {
  s_instance = nullptr;

  // Clean up dithering buffers
  if (errorBuffer) {
    delete[] errorBuffer;
    errorBuffer = nullptr;
  }
  if (nextErrorBuffer) {
    delete[] nextErrorBuffer;
    nextErrorBuffer = nullptr;
  }
  if (s_lineBuffer) {
    delete[] s_lineBuffer;
    s_lineBuffer = nullptr;
  }

  WiFi.mode(WIFI_OFF);
}

void XKCDViewerActivity::fetchComic(int num) {
  if (num > 0) currentComic = num;  // update immediately so navigation advances past failed comics
  state = LOADING;
  render();

  if (WiFi.status() != WL_CONNECTED) {
    state = ERROR;
    render();
    return;
  }

  HTTPClient http;
  String url = num == 0 ? "https://xkcd.com/info.0.json" : "https://xkcd.com/" + String(num) + "/info.0.json";
  http.begin(url);
  http.setTimeout(10000);  // 10 second timeout
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
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

      if (num == 0) {
        maxComic = currentComic;
      }

      state = LOADED;
      scrollOffset = 0;
      imageLoaded = false;

      // Download and display image
      downloadAndDisplayImage();
    } else {
      state = ERROR;
    }
  } else {
    state = ERROR;
  }

  http.end();
  if (state == ERROR) render();
}

void XKCDViewerActivity::downloadAndDisplayImage() {
  // Clear screen and draw title first
  renderer.clearScreen();

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int margin = 20;

  // Comic number and title at top
  String header = "#" + String(currentComic) + ": " + title;
  renderer.drawText(UI_12_FONT_ID, margin, margin, header.c_str(), true);

  // Download PNG to temporary file
  HTTPClient http;
  http.begin(imageUrl);
  http.setTimeout(15000);  // 15 second timeout for images
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);

    GUI.drawButtonHints(renderer, tr(STR_BACK), "",
                      currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                      currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");

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

  int contentLength = http.getSize();
  int downloaded = 0;
  bool writeFailed = false;
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  unsigned long downloadStart = millis();
  while (http.connected() && (contentLength < 0 || downloaded < contentLength)) {
    if (millis() - downloadStart > 20000) break;  // 20-second wall-clock cap
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
      }
    } else {
      delay(1);
    }
  }

  file.close();
  http.end();

  if (writeFailed) {
    Storage.remove("/.crosspoint/xkcd_temp.png");
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
    GUI.drawButtonHints(renderer, tr(STR_BACK), "",
                        currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                        currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  // Show "Decoding..." before PNG decode — decode can take 2-5s on complex images
  renderer.drawText(UI_10_FONT_ID, margin, margin + renderer.getLineHeight(UI_12_FONT_ID) + 20,
                    fork_tr(STR_ONLINE_LOADING), true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  // Decode PNG (draws directly to screen buffer via callback)
  int decodeResult = PNG_DECODE_ERROR;
  if (png.open("/.crosspoint/xkcd_temp.png", pngOpen, pngClose, pngRead, pngSeek, pngDraw) == PNG_SUCCESS) {
    decodeResult = png.decode(NULL, 0);
    png.close();
  }
  if (decodeResult == PNG_SUCCESS) {
    imageLoaded = true;
  } else {
    // Redraw cleanly with failure message instead of leaving "Decoding..." on screen
    renderer.clearScreen();
    String header = "#" + String(currentComic) + ": " + title;
    renderer.drawText(UI_12_FONT_ID, margin, margin, header.c_str(), true);
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, fork_tr(STR_ONLINE_FAILED_LOAD), true);
  }

  // Clean up temp file
  Storage.remove("/.crosspoint/xkcd_temp.png");

  GUI.drawButtonHints(renderer, tr(STR_BACK), "",
                      currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                      currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");

  // Display the complete buffer (title + image + menu)
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

int XKCDViewerActivity::pngDraw(PNGDRAW *pDraw) {
  if (!s_instance) return 1;

  const int screenWidth = s_instance->renderer.getScreenWidth();
  const int screenHeight = s_instance->renderer.getScreenHeight();
  const int topMargin = 60;
  const int bottomMargin = 40;
  const int maxImageWidth = screenWidth - 40;

  // Calculate scale factor
  float scale = 1.0;
  if (pDraw->iWidth > maxImageWidth) {
    scale = (float)maxImageWidth / pDraw->iWidth;
  }

  int scaledWidth = pDraw->iWidth * scale;
  int xOffset = (screenWidth - scaledWidth) / 2;
  int yOffset = topMargin;

  // Initialize dithering buffers on first line. Free the previous comic's
  // buffers before reallocating so a failed allocation never leaves a
  // stale-sized buffer behind.
  if (pDraw->y == 0) {
    ditherWidth = scaledWidth;
    delete[] errorBuffer;
    delete[] nextErrorBuffer;
    delete[] s_lineBuffer;
    errorBuffer = new (std::nothrow) int16_t[ditherWidth + 2]();
    nextErrorBuffer = new (std::nothrow) int16_t[ditherWidth + 2]();
    s_lineBuffer = new (std::nothrow) uint16_t[pDraw->iWidth];
    if (!errorBuffer || !nextErrorBuffer || !s_lineBuffer) {
      DEBUG_PRINTF("[XKCD] OOM allocating dither buffers (width %d)\n", pDraw->iWidth);
      delete[] errorBuffer;     errorBuffer = nullptr;
      delete[] nextErrorBuffer; nextErrorBuffer = nullptr;
      delete[] s_lineBuffer;    s_lineBuffer = nullptr;
      return 0;  // abort decode — PNGdec stops when the draw callback returns 0
    }
  }

  // Buffers may be null if line 0 failed to allocate — abort decode
  if (!errorBuffer || !nextErrorBuffer || !s_lineBuffer) return 0;
  s_instance->png.getLineAsRGB565(pDraw, s_lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  // Process line with Floyd-Steinberg dithering
  for (int x = 0; x < scaledWidth; x++) {
    int sourceX = x / scale;
    if (sourceX >= pDraw->iWidth) sourceX = pDraw->iWidth - 1;

    // Convert to grayscale with gamma correction (simplified)
    uint16_t pixel = s_lineBuffer[sourceX];
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    uint8_t r8 = (r << 3) | (r >> 2);
    uint8_t g8 = (g << 2) | (g >> 4);
    uint8_t b8 = (b << 3) | (b >> 2);
    int gray = (r8 + g8 + b8) / 3;

    // Add accumulated error
    gray += errorBuffer[x + 1];
    gray = constrain(gray, 0, 255);

    // Determine output pixel
    bool isBlack = gray < 128;
    int error = gray - (isBlack ? 0 : 255);

    // Distribute error (80% diffusion to prevent pepper noise)
    error = (error * 4) / 5;
    errorBuffer[x + 2] += (error * 7) >> 4;      // Right: 7/16
    nextErrorBuffer[x] += (error * 3) >> 4;      // Bottom-left: 3/16
    nextErrorBuffer[x + 1] += (error * 5) >> 4;  // Bottom: 5/16
    nextErrorBuffer[x + 2] += error >> 4;        // Bottom-right: 1/16

    // Draw pixel
    int screenX = xOffset + x;
    int screenY = yOffset + (pDraw->y * scale);

    if (screenX >= 0 && screenX < screenWidth && screenY >= 0 && screenY < screenHeight - bottomMargin) {
      s_instance->renderer.drawPixel(screenX, screenY, isBlack);
    }
  }

  // Swap error buffers for next line
  int16_t* temp = errorBuffer;
  errorBuffer = nextErrorBuffer;
  nextErrorBuffer = temp;
  memset(nextErrorBuffer, 0, (ditherWidth + 2) * sizeof(int16_t));

  return 1;
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

    // Image is drawn during PNG decode (via callback)
    // Skip space for image if loaded
    if (imageLoaded) {
      y = height - 150; // Position alt text at bottom
    } else {
      const char* note = fork_tr(STR_ONLINE_IMAGE_LOADING);
      renderer.drawText(UI_10_FONT_ID, margin, y, note, true);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 15;
    }

    // Alt text with wrapping
    const int maxWidth = width - 2 * margin;
    String remaining = alt;

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
        renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str(), true);
      }

      y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
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

    GUI.drawButtonHints(renderer, tr(STR_BACK), "",
                        currentComic > 1 ? fork_tr(STR_BTN_PREV) : "",
                        currentComic < maxComic ? tr(STR_NEXT_FIELD) : "");
  }

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
