#include "../../DebugConfig.h"
#include "WeatherActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <WiFi.h>

#include "../../MappedInputManager.h"
#include "components/UITheme.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"
#include <I18n.h>

void WeatherActivity::onEnter() {
  Activity::onEnter();
  state = LOADING;
  render();
  
  // Enable WiFi and try auto-reconnect with saved credentials
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Auto-reconnect to saved network
  
  // Wait up to 5 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(100);
    attempts++;
  }
  
  // Check if connected with valid IP
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchWeather(true);  // Use cache on first load
  } else {
    state = ERROR;
    render();
  }
}

void WeatherActivity::onExit() {
  Activity::onExit();
  WiFi.mode(WIFI_OFF);
}

void WeatherActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
    return;
  }

  // Refresh on confirm
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && state != LOADING) {
    state = LOADING;
    render();
    fetchWeather();
  }
}

void WeatherActivity::fetchWeather(bool allowCache) {
  if (WiFi.status() != WL_CONNECTED) {
    state = ERROR;
    render();
    return;
  }
  
  // Retry up to 2 times on SSL failure
  OnlineContentFetcher::WeatherData data;
  for (int attempt = 0; attempt < 2; attempt++) {
    data = OnlineContentFetcher::fetchWeather(allowCache);
    if (data.success) break;
    
    if (attempt < 1) {
      DEBUG_PRINTF("[Weather] Fetch failed (attempt %d), retrying...\n", attempt + 1);
      delay(1000);  // Wait 1 second before retry
    }
  }
  
  if (data.success) {
    temperature = data.temperature;
    location = data.location.c_str();
    condition = data.condition.c_str();
    feelsLike = data.feelsLike;
    humidity = data.humidity;
    windSpeed = data.windSpeed;
    
    state = LOADED;
    lastUpdate = millis();
  } else {
    state = ERROR;
  }
  
  render();
}

void WeatherActivity::drawWeatherIcon(int x, int y, int size, const char* cond) {
  String condition = String(cond);
  condition.toLowerCase();
  
  // Sun
  if (condition.indexOf("sunny") >= 0 || condition.indexOf("clear") >= 0) {
    // Circle
    int r = size / 3;
    for (int dy = -r; dy <= r; dy++) {
      int dx = (int)sqrt(r*r - dy*dy);
      renderer.drawLine(x - dx, y + dy, x + dx, y + dy);
    }
    // Rays
    for (int i = 0; i < 8; i++) {
      float angle = i * PI / 4;
      int x1 = x + cos(angle) * r * 1.3;
      int y1 = y + sin(angle) * r * 1.3;
      int x2 = x + cos(angle) * r * 1.8;
      int y2 = y + sin(angle) * r * 1.8;
      renderer.drawLine(x1, y1, x2, y2);
    }
  }
  // Cloud
  else if (condition.indexOf("cloud") >= 0 || condition.indexOf("overcast") >= 0) {
    // Three circles forming cloud
    int r = size / 5;
    for (int i = 0; i < 3; i++) {
      int cx = x - size/4 + i * size/4;
      int cy = y - (i == 1 ? r/2 : 0);
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r*r - dy*dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
  }
  // Rain
  else if (condition.indexOf("rain") >= 0 || condition.indexOf("drizzle") >= 0) {
    // Cloud
    int r = size / 6;
    for (int i = 0; i < 3; i++) {
      int cx = x - size/4 + i * size/4;
      int cy = y - size/4;
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r*r - dy*dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
    // Rain drops
    for (int i = 0; i < 4; i++) {
      int rx = x - size/3 + i * size/5;
      renderer.drawLine(rx, y, rx, y + size/3);
    }
  }
  // Snow
  else if (condition.indexOf("snow") >= 0) {
    // Snowflake
    renderer.drawLine(x, y - size/3, x, y + size/3);
    renderer.drawLine(x - size/3, y, x + size/3, y);
    renderer.drawLine(x - size/4, y - size/4, x + size/4, y + size/4);
    renderer.drawLine(x - size/4, y + size/4, x + size/4, y - size/4);
  }
  // Default: partly cloudy
  else {
    // Sun + cloud
    int r = size / 5;
    // Sun (partial)
    for (int dy = -r; dy <= r; dy++) {
      int dx = (int)sqrt(r*r - dy*dy);
      if (dy < 0) renderer.drawLine(x - dx - size/6, y + dy - size/6, x + dx - size/6, y + dy - size/6);
    }
    // Cloud
    for (int i = 0; i < 2; i++) {
      int cx = x + i * size/5;
      int cy = y + size/8;
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r*r - dy*dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
  }
}

void WeatherActivity::render() {
  renderer.clearScreen();

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Try to load background image from SD card
  bool hasBackground = false;
  if (state == LOADED) {
    hasBackground = loadWeatherBackground(condition.c_str());
  }

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 20, tr(STR_WEATHER_TITLE));

  if (state == LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, tr(STR_ONLINE_LOADING));
  } else if (state == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 - 20, tr(STR_ONLINE_FAILED_LOAD));
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 + 10, tr(STR_ONLINE_CHECK_WIFI));
  } else {
    // Location
    renderer.drawCenteredText(UI_10_FONT_ID, 50, location.c_str());

    // Weather icon (only if no background image)
    if (!hasBackground) {
      drawWeatherIcon(screenWidth / 2, 150, 80, condition.c_str());
    }

    // Temperature (large)
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%d°C", temperature);
    renderer.drawCenteredText(UI_12_FONT_ID, 250, tempStr);

    // Condition
    renderer.drawCenteredText(UI_10_FONT_ID, 280, condition.c_str());

    // Details
    char detailsStr[64];
    snprintf(detailsStr, sizeof(detailsStr), "%s %d°C", tr(STR_WEATHER_FEELS_LIKE), feelsLike);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, detailsStr);

    snprintf(detailsStr, sizeof(detailsStr), "%s %d%%  %s %d km/h",
             tr(STR_WEATHER_HUMIDITY), humidity, tr(STR_WEATHER_WIND), windSpeed);
    renderer.drawCenteredText(UI_10_FONT_ID, 350, detailsStr);

    // Last update
    unsigned long mins = (millis() - lastUpdate) / 60000;
    if (mins == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, 390, tr(STR_WEATHER_JUST_UPDATED));
    } else {
      snprintf(detailsStr, sizeof(detailsStr), "%s %lu %s",
               tr(STR_WEATHER_UPDATED), mins, tr(STR_WEATHER_MIN_AGO));
      renderer.drawCenteredText(UI_10_FONT_ID, 390, detailsStr);
    }
  }

  // Button hints
  const char* confirmText = state == LOADING ? "" : tr(STR_ONLINE_REFRESH);
  const auto labels = mappedInput.mapLabels("Back", confirmText, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawBatteryRight(renderer, Rect{screenWidth - 25, 10, metrics.batteryWidth, metrics.batteryHeight}, false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool WeatherActivity::loadWeatherBackground(const char* cond) {
  String condition = String(cond);
  condition.toLowerCase();
  
  // Map condition to filename
  const char* filename = nullptr;
  if (condition.indexOf("clear") >= 0 || condition.indexOf("sunny") >= 0) {
    filename = "/.crosspoint/weather/clear.bmp";
  } else if (condition.indexOf("snow") >= 0) {
    filename = "/.crosspoint/weather/snowy.bmp";
  } else if (condition.indexOf("rain") >= 0 || condition.indexOf("drizzle") >= 0) {
    filename = "/.crosspoint/weather/rainy.bmp";
  } else if (condition.indexOf("storm") >= 0 || condition.indexOf("thunder") >= 0) {
    filename = "/.crosspoint/weather/stormy.bmp";
  } else if (condition.indexOf("partly") >= 0 || condition.indexOf("partial") >= 0) {
    filename = "/.crosspoint/weather/partly-cloudy.bmp";
  } else if (condition.indexOf("cloud") >= 0 || condition.indexOf("overcast") >= 0) {
    filename = "/.crosspoint/weather/cloudy.bmp";
  }
  
  if (!filename) {
    DEBUG_PRINTLN("[Weather] No filename matched for condition");
    return false;
  }
  
  DEBUG_PRINTF("[Weather] Trying to load: %s\n", filename);
  
  // Try to open and draw the BMP
  FsFile bmpFile;
  if (!Storage.openFileForRead("WEATHER", filename, bmpFile)) {
    DEBUG_PRINTF("[Weather] Failed to open file: %s\n", filename);
    return false;
  }
  
  DEBUG_PRINTLN("[Weather] File opened, parsing headers");
  
  Bitmap bitmap(bmpFile, true);  // Enable dithering for grayscale
  BmpReaderError parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    DEBUG_PRINTF("[Weather] Failed to parse BMP headers: %s\n", Bitmap::errorToString(parseResult));
    bmpFile.close();
    return false;
  }
  
  DEBUG_PRINTF("[Weather] Drawing bitmap: %dx%d\n", bitmap.getWidth(), bitmap.getHeight());
  
  // Draw the bitmap (full screen 480x800)
  renderer.drawBitmap(bitmap, 0, 0, 480, 800);
  bmpFile.close();
  
  DEBUG_PRINTLN("[Weather] Background loaded successfully");
  return true;
}
