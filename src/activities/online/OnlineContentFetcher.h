#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace OnlineContentFetcher {

static constexpr int HTTP_TIMEOUT_MS = 10000;
static constexpr unsigned long WEATHER_CACHE_DURATION_SEC = 1800;  // 30 minutes

// Weather data cache (persists for the session, not across reboots)
struct WeatherCacheData {
  unsigned long cacheTime = 0;
  char location[64] = "";
  float temp = 0.0f;
  float feelsLike = 0.0f;
  char condition[32] = "";
  float humidity = 0.0f;
  float windSpeed = 0.0f;
};
static WeatherCacheData s_weatherCache;

struct WeatherData {
  bool success;
  String location;
  int temperature;
  int feelsLike;
  String condition;
  int humidity;
  int windSpeed;
};

struct WordData {
  bool success;
  String word;
  String definition;
  String example;
};

struct WikipediaData {
  bool success;
  String title;
  String extract;
  String imageUrl;
};

inline WeatherData fetchWeather(bool allowCache = false) {
  WeatherData data = {false, "", 0, 0, "", 0, 0};
  
  // Check cache if allowed
  if (allowCache) {
    unsigned long now = millis() / 1000;
    unsigned long cacheAge = now - s_weatherCache.cacheTime;

    if (cacheAge < WEATHER_CACHE_DURATION_SEC && s_weatherCache.location[0] != '\0') {
      Serial.printf("[Weather] Using cached data (age: %lu sec)\n", cacheAge);
      data.success = true;
      data.location = String(s_weatherCache.location);
      data.temperature = s_weatherCache.temp;
      data.feelsLike = s_weatherCache.feelsLike;
      data.condition = String(s_weatherCache.condition);
      data.humidity = s_weatherCache.humidity;
      data.windSpeed = s_weatherCache.windSpeed;
      return data;
    }
  }
  
  HTTPClient http;
  http.begin("https://wttr.in/?format=j1");
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  Serial.printf("[Weather] HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.printf("[Weather] Payload length: %d\n", payload.length());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    Serial.printf("[Weather] JSON parse: %s\n", error.c_str());
    
    if (!error && 
        doc["current_condition"].size() > 0 &&
        doc["current_condition"][0]["weatherDesc"].size() > 0 &&
        doc["nearest_area"].size() > 0 &&
        doc["nearest_area"][0]["areaName"].size() > 0) {
      
      Serial.println("[Weather] All checks passed, extracting data");
      
      JsonObject current = doc["current_condition"][0];
      data.temperature = current["temp_C"].as<int>();
      data.feelsLike = current["FeelsLikeC"].as<int>();
      data.condition = current["weatherDesc"][0]["value"].as<String>();
      data.humidity = current["humidity"].as<int>();
      data.windSpeed = current["windspeedKmph"].as<int>();
      
      JsonObject nearest = doc["nearest_area"][0];
      String rawLocation = nearest["areaName"][0]["value"].as<String>();
      
      // Handle UTF-8 encoding - copy only valid ASCII chars
      for (size_t i = 0; i < rawLocation.length(); i++) {
        char c = rawLocation[i];
        if (c >= 32 && c <= 126) data.location += c;
      }
      if (data.location.length() == 0) data.location = "Unknown";
      
      Serial.printf("[Weather] Temp: %d, Location: %s\n", data.temperature, data.location.c_str());
      data.success = true;
      
      // Save to cache
      s_weatherCache.cacheTime = millis() / 1000;
      strncpy(s_weatherCache.location, data.location.c_str(), sizeof(s_weatherCache.location) - 1);
      s_weatherCache.location[sizeof(s_weatherCache.location) - 1] = '\0';
      s_weatherCache.temp = data.temperature;
      s_weatherCache.feelsLike = data.feelsLike;
      strncpy(s_weatherCache.condition, data.condition.c_str(), sizeof(s_weatherCache.condition) - 1);
      s_weatherCache.condition[sizeof(s_weatherCache.condition) - 1] = '\0';
      s_weatherCache.humidity = data.humidity;
      s_weatherCache.windSpeed = data.windSpeed;
      Serial.println("[Weather] Cached for 30 minutes");
    } else {
      Serial.println("[Weather] Validation failed");
    }
  } else {
    Serial.printf("[Weather] HTTP failed: %d\n", httpCode);
  }
  
  http.end();
  return data;
}

inline WordData fetchWordOfDay() {
  WordData data = {false, "", "", ""};
  
  HTTPClient http;
  http.begin("https://random-word-api.herokuapp.com/word?number=1");
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  if (http.GET() == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok && doc.size() > 0) {
      data.word = doc[0].as<String>();
    }
  }
  http.end();
  
  if (data.word.length() == 0) return data;
  
  String dictUrl = "https://api.dictionaryapi.dev/api/v2/entries/en/" + data.word;
  http.begin(dictUrl);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  if (http.GET() == 200) {
    String payload = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok && 
        doc.size() > 0 &&
        doc[0]["meanings"].size() > 0 &&
        doc[0]["meanings"][0]["definitions"].size() > 0) {
      JsonObject meaning = doc[0]["meanings"][0];
      data.definition = meaning["definitions"][0]["definition"].as<String>();
      
      if (meaning["definitions"][0]["example"].is<const char*>()) {
        data.example = meaning["definitions"][0]["example"].as<String>();
      }
      
      data.success = true;
    }
  } else {
    // No definition found, just show the word
    data.definition = "Definition not available";
    data.success = true;
  }
  http.end();
  
  return data;
}

inline WikipediaData fetchWikipediaRandom() {
  WikipediaData data = {false, "", "", ""};
  
  HTTPClient http;
  http.begin("https://en.wikipedia.org/api/rest_v1/page/random/summary");
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("CrossPointReader/1.0 (https://github.com/daveallie/crosspoint-reader)");
  
  int httpCode = http.GET();
  Serial.printf("[WIKI API] HTTP code: %d\n", httpCode);
  
  // Accept 200 (success) or 303 (redirect that wasn't followed)
  if (httpCode == 200 || httpCode == 303) {
    String payload = http.getString();
    Serial.printf("[WIKI API] Payload length: %d\n", payload.length());
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    Serial.printf("[WIKI API] JSON parse: %s\n", err.c_str());
    
    if (err == DeserializationError::Ok) {
      data.title = doc["title"].as<String>();
      data.extract = doc["extract"].as<String>();
      
      Serial.printf("[WIKI API] Title: %s\n", data.title.c_str());
      
      // Get thumbnail image if available
      if (doc["thumbnail"].is<JsonObject>()) {
        JsonObject thumb = doc["thumbnail"];
        data.imageUrl = thumb["source"].as<String>();
      }
      
      // If extract is short, try to get more from extract_html
      if (data.extract.length() < 500 && doc["extract_html"].is<const char*>()) {
        String extractHtml = doc["extract_html"].as<String>();
        if (extractHtml.length() > 0) {
          // Convert known block elements to newlines before stripping
          extractHtml.replace("</p>", "\n\n");
          extractHtml.replace("<br>", "\n");
          extractHtml.replace("<br/>", "\n");
          extractHtml.replace("<br />", "\n");

          // Strip all remaining HTML tags
          String cleaned;
          cleaned.reserve(extractHtml.length());
          bool inTag = false;
          for (size_t i = 0; i < extractHtml.length(); i++) {
            char c = extractHtml[i];
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; continue; }
            if (!inTag) cleaned += c;
          }
          data.extract = cleaned;
        }
      }
      
      data.success = true;
    }
  }
  
  http.end();
  return data;
}

inline bool ensureWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(100);
    attempts++;
  }
  
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace OnlineContentFetcher
