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

// Static pointer for PNG callback
static XKCDViewerActivity* s_instance = nullptr;
static FsFile s_pngFile;

// Floyd-Steinberg dithering buffers
static int16_t* errorBuffer = nullptr;
static int16_t* nextErrorBuffer = nullptr;
static int ditherWidth = 0;

// PNG file callbacks
void* pngOpen(const char *filename, int32_t *size) {
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
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Auto-reconnect to saved network
  
  // Wait up to 5 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(100);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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
  
  WiFi.mode(WIFI_OFF);
}

void XKCDViewerActivity::fetchComic(int num) {
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
    
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
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
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_ONLINE_FAILED_LOAD), true);
    
    GUI.drawButtonHints(renderer, "Back", "", "Prev", "Next");
    
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  
  // Save to SD card temporarily
  FsFile file;
  if (!Storage.openFileForWrite("XKCD", "/.crosspoint/xkcd_temp.png", file)) {
    http.end();
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2, tr(STR_ONLINE_FAILED_LOAD), true);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  
  while (http.connected() && stream->available()) {
    size_t size = stream->available();
    if (size > sizeof(buffer)) size = sizeof(buffer);
    size_t bytesRead = stream->readBytes(buffer, size);
    file.write(buffer, bytesRead);
  }
  
  file.close();
  http.end();
  
  // Decode PNG (draws directly to screen buffer via callback)
  if (png.open("/.crosspoint/xkcd_temp.png", pngOpen, pngClose, pngRead, pngSeek, pngDraw) == PNG_SUCCESS) {
    png.decode(NULL, 0);
    png.close();
    imageLoaded = true;
  }
  
  // Clean up temp file
  Storage.remove("/.crosspoint/xkcd_temp.png");
  
  GUI.drawButtonHints(renderer, "Back", "", "Prev", "Next");
  
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
  
  // Initialize dithering buffers on first line
  if (pDraw->y == 0) {
    ditherWidth = scaledWidth;
    if (errorBuffer) delete[] errorBuffer;
    if (nextErrorBuffer) delete[] nextErrorBuffer;
    errorBuffer = new int16_t[ditherWidth + 2]();
    nextErrorBuffer = new int16_t[ditherWidth + 2]();
  }
  
  // Convert PNG line to RGB565
  uint16_t lineBuffer[800];
  s_instance->png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  
  // Process line with Floyd-Steinberg dithering
  for (int x = 0; x < scaledWidth; x++) {
    int sourceX = x / scale;
    if (sourceX >= pDraw->iWidth) sourceX = pDraw->iWidth - 1;
    
    // Convert to grayscale with gamma correction (simplified)
    uint16_t pixel = lineBuffer[sourceX];
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    int gray = (r * 8 + g * 4 + b * 8) / 3;
    
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
    const char* msg = tr(STR_ONLINE_LOADING);
    int textWidth = renderer.getTextWidth(UI_10_FONT_ID, msg);
    renderer.drawText(UI_10_FONT_ID, (width - textWidth) / 2, height / 2, msg, true);
  } else if (state == ERROR) {
    const char* msg = tr(STR_ONLINE_FAILED_LOAD);
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
      const char* note = tr(STR_ONLINE_IMAGE_LOADING);
      renderer.drawText(UI_10_FONT_ID, margin, y, note, true);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 15;
    }
    
    // Alt text with wrapping
    const int maxWidth = width - 2 * margin;
    String remaining = alt;
    
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
        renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str(), true);
      }
      
      y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
      remaining = remaining.substring(breakPos);
      remaining.trim();
    }
    
    y += 20;
    
    // Navigation hint
    const char* nav = tr(STR_XKCD_NAV);
    if (y >= 0 && y < height) {
      renderer.drawText(UI_10_FONT_ID, margin, y, nav, true);
    }
    y += renderer.getLineHeight(UI_10_FONT_ID);
    
    maxScroll = max(0, y - height + margin);
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
