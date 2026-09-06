#include "WeatherActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "../../MappedInputManager.h"
#include "../../fontIds.h"
#include "OnlineContentFetcher.h"
#include "components/UITheme.h"

void WeatherActivity::onEnter() {
  Activity::onEnter();
  state = LOADING;
  render();

  // Connect using CrossPoint's saved WiFi credentials
  if (OnlineContentFetcher::ensureWiFi(&mappedInput) && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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

  OnlineContentFetcher::WeatherData data = OnlineContentFetcher::fetchWeather(allowCache, &mappedInput);

  if (data.cancelled) {
    // User pressed Back mid-fetch — leave the activity instead of rendering
    if (onBack) onBack();
    return;
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
    // A solid black disk reads as a heavy blob on a 1-bit display and
    // dominates the screen next to the light photographic backgrounds.
    // Use a dithered gray glow with a thin black outline instead — same
    // technique the theme uses for soft rounded corners (fillRectDither).
    const int r = size / 4;
    for (int dy = -r; dy <= r; dy++) {
      const int dx = (int)sqrt(r * r - dy * dy);
      renderer.fillRectDither(x - dx, y + dy, 2 * dx + 1, 1, Color::LightGray);
    }
    // Thin ring outline for definition (four quarter-arcs sharing one center)
    renderer.drawArc(r, x, y, 1, 1, 2, true);
    renderer.drawArc(r, x, y, 1, -1, 2, true);
    renderer.drawArc(r, x, y, -1, 1, 2, true);
    renderer.drawArc(r, x, y, -1, -1, 2, true);
    // Short, thin rays — gray dithered dashes instead of thick black spikes
    for (int i = 0; i < 8; i++) {
      const float angle = i * (float)PI / 4;
      const int x1 = x + (int)(cos(angle) * r * 1.3f);
      const int y1 = y + (int)(sin(angle) * r * 1.3f);
      const int x2 = x + (int)(cos(angle) * r * 1.7f);
      const int y2 = y + (int)(sin(angle) * r * 1.7f);
      renderer.drawLine(x1, y1, x2, y2, 2, true);
    }
  }
  // Cloud
  else if (condition.indexOf("cloud") >= 0 || condition.indexOf("overcast") >= 0) {
    // Three circles forming cloud
    int r = size / 5;
    for (int i = 0; i < 3; i++) {
      int cx = x - size / 4 + i * size / 4;
      int cy = y - (i == 1 ? r / 2 : 0);
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r * r - dy * dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
  }
  // Rain
  else if (condition.indexOf("rain") >= 0 || condition.indexOf("drizzle") >= 0) {
    // Cloud
    int r = size / 6;
    for (int i = 0; i < 3; i++) {
      int cx = x - size / 4 + i * size / 4;
      int cy = y - size / 4;
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r * r - dy * dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
    // Rain drops
    for (int i = 0; i < 4; i++) {
      int rx = x - size / 3 + i * size / 5;
      renderer.drawLine(rx, y, rx, y + size / 3);
    }
  }
  // Storm / Thunder
  else if (condition.indexOf("storm") >= 0 || condition.indexOf("thunder") >= 0) {
    // Cloud
    int r = size / 6;
    for (int i = 0; i < 3; i++) {
      int cx = x - size / 4 + i * size / 4;
      int cy = y - size / 4;
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r * r - dy * dy);
        renderer.drawLine(cx - dx, cy + dy, cx + dx, cy + dy);
      }
    }
    // Lightning bolt — thick strokes so the jagged shape reads clearly
    const int boltWidth = std::max(2, size / 24);
    renderer.drawLine(x, y - size / 8, x - size / 8, y + size / 8, boltWidth, true);
    renderer.drawLine(x - size / 8, y + size / 8, x + size / 16, y + size / 16, boltWidth, true);
    renderer.drawLine(x + size / 16, y + size / 16, x - size / 8, y + size / 2, boltWidth, true);
  }
  // Snow
  else if (condition.indexOf("snow") >= 0) {
    // Snowflake
    renderer.drawLine(x, y - size / 3, x, y + size / 3);
    renderer.drawLine(x - size / 3, y, x + size / 3, y);
    renderer.drawLine(x - size / 4, y - size / 4, x + size / 4, y + size / 4);
    renderer.drawLine(x - size / 4, y + size / 4, x + size / 4, y - size / 4);
  }
  // Default: partly cloudy
  else {
    // Sun + cloud
    int r = size / 5;
    // Sun (partial)
    for (int dy = -r; dy <= r; dy++) {
      int dx = (int)sqrt(r * r - dy * dy);
      if (dy < 0) renderer.drawLine(x - dx - size / 6, y + dy - size / 6, x + dx - size / 6, y + dy - size / 6);
    }
    // Cloud
    for (int i = 0; i < 2; i++) {
      int cx = x + i * size / 5;
      int cy = y + size / 8;
      for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt(r * r - dy * dy);
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
  renderer.drawCenteredText(UI_12_FONT_ID, 20, fork_tr(STR_WEATHER_TITLE));

  if (state == LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, fork_tr(STR_ONLINE_LOADING));
  } else if (state == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 - 20, fork_tr(STR_ONLINE_FAILED_LOAD));
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 + 10, fork_tr(STR_ONLINE_CHECK_WIFI));
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
    snprintf(detailsStr, sizeof(detailsStr), "%s %d°C", fork_tr(STR_WEATHER_FEELS_LIKE), feelsLike);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, detailsStr);

    snprintf(detailsStr, sizeof(detailsStr), "%s %d%%  %s %d km/h", fork_tr(STR_WEATHER_HUMIDITY), humidity,
             fork_tr(STR_WEATHER_WIND), windSpeed);
    renderer.drawCenteredText(UI_10_FONT_ID, 350, detailsStr);

    // Last update
    unsigned long mins = (millis() - lastUpdate) / 60000;
    if (mins == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, 390, fork_tr(STR_WEATHER_JUST_UPDATED));
    } else {
      snprintf(detailsStr, sizeof(detailsStr), "%s %lu %s", fork_tr(STR_WEATHER_UPDATED), mins,
               fork_tr(STR_WEATHER_MIN_AGO));
      renderer.drawCenteredText(UI_10_FONT_ID, 390, detailsStr);
    }
  }

  // Button hints
  const char* confirmText = state == LOADING ? "" : fork_tr(STR_ONLINE_REFRESH);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmText, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Battery
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawBatteryLeft(renderer, Rect{screenWidth - 25, 10, metrics.batteryWidth, metrics.batteryHeight}, false);

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
    LOG_DBG("WTHR", "No filename matched for condition");
    return false;
  }

  LOG_DBG("WTHR", "Trying to load: %s", filename);

  // Try to open and draw the BMP
  HalFile bmpFile;
  if (!Storage.openFileForRead("WEATHER", filename, bmpFile)) {
    LOG_DBG("WTHR", "Failed to open file: %s", filename);
    return false;
  }

  LOG_DBG("WTHR", "File opened, parsing headers");

  Bitmap bitmap(bmpFile, true);  // Enable dithering for grayscale
  BmpReaderError parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_DBG("WTHR", "Failed to parse BMP headers: %s", Bitmap::errorToString(parseResult));
    bmpFile.close();
    return false;
  }

  LOG_DBG("WTHR", "Drawing bitmap: %dx%d", bitmap.getWidth(), bitmap.getHeight());

  // Draw the bitmap (full screen 480x800)
  renderer.drawBitmap(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  bmpFile.close();

  LOG_DBG("WTHR", "Background loaded successfully");
  return true;
}
