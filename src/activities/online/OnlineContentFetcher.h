#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "../../MappedInputManager.h"
#include "../../WifiCredentialStore.h"
#include "OnlineCache.h"

namespace OnlineContentFetcher {

// http.GET() blocks with no polling until connect+response completes or one
// of these fires — there is no way to interrupt a blocking socket read
// without a background task (no precedent anywhere in this codebase), so
// responsiveness to Back is bounded by these, not by pollCancel() alone.
static constexpr int HTTP_CONNECT_TIMEOUT_MS = 4000;
static constexpr int HTTP_TIMEOUT_MS = 6000;
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
inline WeatherCacheData s_weatherCache{};

struct WeatherData {
  bool success;
  bool cancelled = false;
  String location;
  int temperature;
  int feelsLike;
  String condition;
  int humidity;
  int windSpeed;
};

struct WordData {
  bool success;
  bool cancelled = false;
  String word;
  String definition;
  String example;
};

struct WikipediaData {
  bool success;
  bool cancelled = false;
  String title;
  String extract;
  String imageUrl;
};

// Pump the input manager and report whether the user pressed Back. Fetches
// run synchronously on the main task, so without this the Back press would
// sit unseen until the fetch finishes. Safe to call in a blocking loop —
// it is the same update() the main loop performs each frame.
inline bool pollCancel(MappedInputManager* input) {
  if (!input) return false;
  input->update();
  return input->wasPressed(MappedInputManager::Button::Back);
}

inline WeatherData fetchWeather(bool allowCache = false, MappedInputManager* cancelInput = nullptr) {
  WeatherData data{};
  
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
  
  if (pollCancel(cancelInput)) { data.cancelled = true; return data; }

  HTTPClient http;
  http.begin("https://wttr.in/?format=j1");
  // A fresh HTTPClient is created per fetch (not actually reused across
  // calls), so the default _reuse=true just leaks the socket whenever the
  // server sends Connection: keep-alive — ESP32-C3's small fixed lwIP socket
  // pool can silently exhaust after a handful of leaked sockets, after which
  // new connections stall and time out instead of failing fast (confirmed on
  // XKCDViewerActivity).
  http.setReuse(false);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Each GET() blocks solid for up to connect+response timeout with zero
  // polling — a retry here used to double that block, which is what made
  // Back feel unresponsive. One GET, cancel-checked immediately after.
  int httpCode = http.GET();
  Serial.printf("[Weather] HTTP Code: %d\n", httpCode);
  if (pollCancel(cancelInput)) { http.end(); data.cancelled = true; return data; }

  if (httpCode == 200) {
    // Read the ~40KB body into a String (getString's drain loop handles
    // wttr.in's missing Content-Length correctly; ArduinoJson's stream
    // reader hits IncompleteInput on this server). The filter below keeps
    // the parse tree tiny, which was the real memory spike.
    String payload = http.getString();
    Serial.printf("[Weather] Payload length: %d\n", payload.length());

    JsonDocument filter;
    filter["current_condition"][0]["temp_C"] = true;
    filter["current_condition"][0]["FeelsLikeC"] = true;
    filter["current_condition"][0]["humidity"] = true;
    filter["current_condition"][0]["windspeedKmph"] = true;
    filter["current_condition"][0]["weatherDesc"][0]["value"] = true;
    filter["nearest_area"][0]["areaName"][0]["value"] = true;

    JsonDocument doc;
    DeserializationError error =
        deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    Serial.printf("[Weather] JSON parse: %s\n", error.c_str());

    // wttr.in often truncates the tail of the payload (the multi-day forecast
    // array, which we don't read). The fields we need are at the start, so
    // accept IncompleteInput and let the field validation below decide.
    if ((!error || error == DeserializationError::IncompleteInput) &&
        doc["current_condition"].size() > 0 &&
        doc["current_condition"][0]["weatherDesc"].size() > 0 &&
        !doc["current_condition"][0]["temp_C"].isNull() &&
        !doc["current_condition"][0]["FeelsLikeC"].isNull() &&
        !doc["current_condition"][0]["humidity"].isNull() &&
        !doc["current_condition"][0]["windspeedKmph"].isNull() &&
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
      // Persist for the sleep-screen overlay (survives deep sleep)
      OnlineCache::saveWeather(data.location.c_str(), data.condition.c_str(), data.temperature);
    } else {
      Serial.println("[Weather] Validation failed");
    }
  } else {
    Serial.printf("[Weather] HTTP failed: %d\n", httpCode);
  }
  
  http.end();
  return data;
}

inline WordData fetchWordOfDay(MappedInputManager* cancelInput = nullptr) {
  WordData data{};
  
  HTTPClient http;
  http.begin("https://random-word-api.herokuapp.com/word?number=1");
  // See setReuse comment in fetchWeather() above.
  http.setReuse(false);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
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
  if (pollCancel(cancelInput)) { data.cancelled = true; return data; }

  String dictUrl = "https://api.dictionaryapi.dev/api/v2/entries/en/" + data.word;
  http.begin(dictUrl);
  http.setReuse(false);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
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

  if (data.success) {
    // Persist for the sleep-screen overlay (survives deep sleep)
    OnlineCache::saveWord(data.word.c_str());
  }

  return data;
}

inline WikipediaData fetchWikipediaRandom(MappedInputManager* cancelInput = nullptr) {
  WikipediaData data{};
  
  HTTPClient http;
  http.begin("https://en.wikipedia.org/api/rest_v1/page/random/summary");
  // See setReuse comment in fetchWeather() above.
  http.setReuse(false);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("CrossPointReader/1.0 (https://github.com/daveallie/crosspoint-reader)");
  
  int httpCode = http.GET();
  Serial.printf("[WIKI API] HTTP code: %d\n", httpCode);
  if (pollCancel(cancelInput)) { http.end(); data.cancelled = true; return data; }

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
          // Decode common HTML entities
          data.extract.replace("&amp;", "&");
          data.extract.replace("&lt;", "<");
          data.extract.replace("&gt;", ">");
          data.extract.replace("&quot;", "\"");
          data.extract.replace("&apos;", "'");
          data.extract.replace("&nbsp;", " ");
          data.extract.replace("&#160;", " ");
          data.extract.replace("&#8211;", "\xe2\x80\x93");
          data.extract.replace("&#8212;", "\xe2\x80\x94");
          data.extract.replace("&#8216;", "'");
          data.extract.replace("&#8217;", "'");
          data.extract.replace("&#8220;", "\"");
          data.extract.replace("&#8221;", "\"");
        }
      }

      data.success = true;
    }
  }
  
  http.end();
  return data;
}

inline bool ensureWiFi(MappedInputManager* cancelInput = nullptr) {
  if (WiFi.status() == WL_CONNECTED) return true;

  // Upstream suppresses the SDK's NVS auto-connect (WiFi.persistent(false) +
  // disconnect(true, true) in WifiSelectionActivity), so an argless
  // WiFi.begin() has no stored network to join. Connect explicitly with the
  // credentials CrossPoint keeps in WIFI_STORE, preferring the last-used one.
  if (WIFI_STORE.getCredentials().empty()) WIFI_STORE.loadFromFile();

  const std::string& lastSsid = WIFI_STORE.getLastConnectedSsid();
  const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
  if (!cred && !WIFI_STORE.getCredentials().empty()) {
    cred = &WIFI_STORE.getCredentials().front();
  }
  if (!cred) {
    Serial.println("[WiFi] No saved credentials — connect once via Settings > WiFi");
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }

  // Wait up to 10 seconds for association + DHCP; Back aborts the wait
  for (int attempts = 0; WiFi.status() != WL_CONNECTED && attempts < 100; attempts++) {
    if (pollCancel(cancelInput)) return false;
    delay(100);
  }
  Serial.printf("[WiFi] ensureWiFi: %s (ssid: %s)\n",
                WiFi.status() == WL_CONNECTED ? "connected" : "FAILED", cred->ssid.c_str());
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace OnlineContentFetcher
