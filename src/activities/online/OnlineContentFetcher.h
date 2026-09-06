#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cctype>
#include <cstring>
#include <ctime>
#include <optional>

#include "../../CrossPointSettings.h"
#include "../../ForkSettings.h"
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
      LOG_INF("WTHR", "Using cached data (age: %lu sec)", cacheAge);
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

  if (pollCancel(cancelInput)) {
    data.cancelled = true;
    return data;
  }

  // Empty FORK_SETTINGS.weatherLocation -> bare URL, wttr.in geolocates the
  // request by public IP (ISP-dependent, can land on a neighbouring district).
  // Otherwise URL-encode the configured place name / "lat,lon" into the path.
  char url[192];
  int urlLen = snprintf(url, sizeof(url), "https://wttr.in/");
  for (const char* c = FORK_SETTINGS.weatherLocation; *c && urlLen < (int)sizeof(url) - 16; c++) {
    const unsigned char ch = static_cast<unsigned char>(*c);
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ',' || ch == '~') {
      url[urlLen++] = *c;
    } else if (ch == ' ') {
      url[urlLen++] = '+';
    } else {
      urlLen += snprintf(url + urlLen, sizeof(url) - urlLen, "%%%02X", ch);
    }
  }
  snprintf(url + urlLen, sizeof(url) - urlLen, "?format=j1");
  LOG_INF("WTHR", "GET %s", url);

  HTTPClient http;
  http.begin(url);
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
  LOG_INF("WTHR", "HTTP Code: %d", httpCode);
  if (pollCancel(cancelInput)) {
    http.end();
    data.cancelled = true;
    return data;
  }

  if (httpCode == 200) {
    // Read the ~40KB body into a String (getString's drain loop handles
    // wttr.in's missing Content-Length correctly; ArduinoJson's stream
    // reader hits IncompleteInput on this server). The filter below keeps
    // the parse tree tiny, which was the real memory spike.
    String payload = http.getString();
    LOG_INF("WTHR", "Payload length: %d", payload.length());

    JsonDocument filter;
    filter["current_condition"][0]["temp_C"] = true;
    filter["current_condition"][0]["FeelsLikeC"] = true;
    filter["current_condition"][0]["humidity"] = true;
    filter["current_condition"][0]["windspeedKmph"] = true;
    filter["current_condition"][0]["weatherDesc"][0]["value"] = true;
    filter["nearest_area"][0]["areaName"][0]["value"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    LOG_INF("WTHR", "JSON parse: %s", error.c_str());

    // wttr.in often truncates the tail of the payload (the multi-day forecast
    // array, which we don't read). The fields we need are at the start, so
    // accept IncompleteInput and let the field validation below decide.
    if ((!error || error == DeserializationError::IncompleteInput) && doc["current_condition"].size() > 0 &&
        doc["current_condition"][0]["weatherDesc"].size() > 0 && !doc["current_condition"][0]["temp_C"].isNull() &&
        !doc["current_condition"][0]["FeelsLikeC"].isNull() && !doc["current_condition"][0]["humidity"].isNull() &&
        !doc["current_condition"][0]["windspeedKmph"].isNull() && doc["nearest_area"].size() > 0 &&
        doc["nearest_area"][0]["areaName"].size() > 0) {
      LOG_INF("WTHR", "All checks passed, extracting data");

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

      LOG_INF("WTHR", "Temp: %d, Location: %s", data.temperature, data.location.c_str());
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
      LOG_INF("WTHR", "Cached for 30 minutes");
      // Persist for the sleep-screen overlay (survives deep sleep)
      OnlineCache::saveWeather(data.location.c_str(), data.condition.c_str(), data.temperature);
    } else {
      LOG_ERR("WTHR", "Validation failed");
    }
  } else {
    LOG_ERR("WTHR", "HTTP failed: %d", httpCode);
  }

  http.end();
  return data;
}

// ---- wall clock -------------------------------------------------------------
inline bool wallClockKnown() { return time(nullptr) > 1000000000L; }

// Set the system clock from NTP (WiFi must be up). HalClock::syncFromNTP() is
// RTC-only and returns false on the X4, so poll SNTP here. Bounded wait.
inline void syncWallClock(unsigned long maxWaitMs = 4000) {
  if (wallClockKnown()) return;
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  const unsigned long start = millis();
  while (millis() - start < maxWaitMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || wallClockKnown()) break;
    delay(100);
  }
  LOG_INF("CLK", "NTP %s", wallClockKnown() ? "synced" : "not synced");
}

// ---- Wiktionary Word of the Day --------------------------------------------
// Reduce wikitext markup to plain text. Handles the constructs the WOTD
// template actually uses: {{m|en|term}}, {{l|en|term}}, {{w|Title|text}},
// {{lb|en|label,...}} -> "(label, ...)", [[page#anchor|text]], ''italics''.
// Anything else in {{ }} is dropped. Innermost-first so nesting works.
inline String stripWikitext(String text) {
  for (int guard = 0; guard < 64; guard++) {
    const int open = text.lastIndexOf("{{");
    if (open < 0) break;
    const int close = text.indexOf("}}", open);
    if (close < 0) {
      text.remove(open);
      break;
    }
    const String inner = text.substring(open + 2, close);
    // Split on '|' and keep positional params only (named ones contain '=')
    String parts[12];
    int count = 0;
    int from = 0;
    while (count < 12) {
      const int bar = inner.indexOf('|', from);
      String piece = bar < 0 ? inner.substring(from) : inner.substring(from, bar);
      piece.trim();
      if (piece.indexOf('=') < 0 || count == 0) parts[count++] = piece;
      if (bar < 0) break;
      from = bar + 1;
    }
    const String& name = parts[0];
    String repl;
    if (name == "m" || name == "l" || name == "mention" || name == "link" || name == "m+" || name == "ll") {
      if (count >= 3) repl = parts[2];
    } else if (name == "lb" || name == "label" || name == "lbl") {
      for (int i = 2; i < count; i++) repl += (i > 2 ? ", " : "(") + parts[i];
      if (count > 2) repl += ")";
    } else if (name == "q" || name == "qual" || name == "qualifier" || name == "gloss" || name == "gl") {
      if (count >= 2) repl = "(" + parts[1] + ")";
    } else if (name == "w" || name == "W") {
      if (count >= 2) repl = parts[count - 1];
    }
    text = text.substring(0, open) + repl + text.substring(close + 2);
  }
  for (int guard = 0; guard < 64; guard++) {
    const int open = text.indexOf("[[");
    if (open < 0) break;
    const int close = text.indexOf("]]", open);
    if (close < 0) {
      text.remove(open, 2);
      continue;
    }
    String inner = text.substring(open + 2, close);
    const int bar = inner.lastIndexOf('|');
    if (bar >= 0) {
      inner = inner.substring(bar + 1);
    } else {
      const int hash = inner.indexOf('#');
      if (hash >= 0) inner = inner.substring(0, hash);
    }
    text = text.substring(0, open) + inner + text.substring(close + 2);
  }
  text.replace("'''", "");
  text.replace("''", "");
  text.replace("&nbsp;", " ");
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");
  text.trim();
  return text;
}

inline const char* expandPartOfSpeech(const String& pos) {
  if (pos == "n") return "noun";
  if (pos == "v") return "verb";
  if (pos == "adj") return "adjective";
  if (pos == "adv") return "adverb";
  if (pos == "prep") return "preposition";
  if (pos == "interj" || pos == "intj") return "interjection";
  if (pos == "pron") return "pronoun";
  if (pos == "conj") return "conjunction";
  if (pos == "num") return "numeral";
  return pos.c_str();
}

// Today's curated Wiktionary Word of the Day: ~1-2KB of JSON holding a
// {{WOTD|word|pos|definition|...}} template. Needs the wall clock for the page
// date (local date per the upstream clock-offset setting).
inline WordData fetchWiktionaryWotd(MappedInputManager* cancelInput) {
  WordData data{};
  syncWallClock();
  if (!wallClockKnown()) {
    LOG_ERR("WOTD", "No wall clock, cannot pick today's page");
    return data;
  }
  static constexpr const char* MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                           "July",    "August",   "September", "October", "November", "December"};
  const long offsetSec = (static_cast<long>(SETTINGS.clockUtcOffsetQ) - 48) * 15 * 60;
  const time_t localNow = time(nullptr) + offsetSec;
  struct tm tmLocal;
  gmtime_r(&localNow, &tmLocal);
  char url[224];
  snprintf(url, sizeof(url),
           "https://en.wiktionary.org/w/api.php?action=parse&page=Wiktionary:Word_of_the_day/%d/%s_%d"
           "&prop=wikitext&format=json&formatversion=2",
           tmLocal.tm_year + 1900, MONTHS[tmLocal.tm_mon], tmLocal.tm_mday);

  HTTPClient http;
  http.begin(url);
  http.setReuse(false);
  http.setUserAgent("CrossPointReader-fork/1.0 (e-reader; word of the day)");  // Wikimedia asks for a UA
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int code = http.GET();
  LOG_INF("WOTD", "Wiktionary GET -> %d", code);
  if (code != 200) {
    http.end();
    return data;
  }
  if (pollCancel(cancelInput)) {
    http.end();
    data.cancelled = true;
    return data;
  }
  String payload = http.getString();
  http.end();

  JsonDocument filter;
  filter["parse"]["wikitext"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    LOG_ERR("WOTD", "JSON parse failed");
    return data;
  }
  const char* wikitext = doc["parse"]["wikitext"] | "";
  const char* tpl = strstr(wikitext, "{{WOTD|");
  if (!tpl) {
    LOG_ERR("WOTD", "No WOTD template on page");
    return data;
  }

  // Top-level split of the template params: '|' counts only at nesting depth 1.
  String params[4];
  int count = 0;
  int depth = 0;
  String current;
  for (const char* c = tpl; *c && count < 4; c++) {
    if (c[0] == '{' && c[1] == '{') {
      depth++;
      if (depth > 1) current += "{{";
      c++;
      continue;
    }
    if (c[0] == '[' && c[1] == '[') {
      depth++;
      current += "[[";
      c++;
      continue;
    }
    if (c[0] == '}' && c[1] == '}') {
      depth--;
      if (depth == 0) {
        params[count++] = current;
        break;
      }
      current += "}}";
      c++;
      continue;
    }
    if (c[0] == ']' && c[1] == ']') {
      depth--;
      current += "]]";
      c++;
      continue;
    }
    if (*c == '|' && depth == 1) {
      params[count++] = current;
      current = "";
      continue;
    }
    current += *c;
  }
  // params[0] = "WOTD", [1] = word, [2] = pos, [3] = definition (first sense line)
  if (count < 4 || params[1].isEmpty()) {
    LOG_ERR("WOTD", "Unexpected WOTD template shape (%d params)", count);
    return data;
  }
  String definition = params[3];
  const int nl = definition.indexOf('\n');
  if (nl >= 0) definition = definition.substring(0, nl);
  definition = stripWikitext(definition);
  if (definition.isEmpty()) return data;

  data.word = stripWikitext(params[1]);
  params[2].trim();
  data.definition = String("(") + expandPartOfSpeech(params[2]) + ") " + definition;
  data.success = true;
  return data;
}

// Fallback: a random dictionary-list word looked up on dictionaryapi.dev. The
// list is full of inflected/obscure forms the dictionary lacks, so try a few
// and only report success with a real definition (never cache a bare word).
inline WordData fetchRandomWordWithDefinition(MappedInputManager* cancelInput) {
  WordData data{};
  constexpr int MAX_ATTEMPTS = 3;
  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    data = WordData{};
    HTTPClient http;
    http.begin("https://random-word-api.herokuapp.com/word?number=1");
    http.setReuse(false);  // see setReuse comment in fetchWeather()
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
    if (data.word.isEmpty()) return data;
    if (pollCancel(cancelInput)) {
      data.cancelled = true;
      return data;
    }

    String dictUrl = "https://api.dictionaryapi.dev/api/v2/entries/en/" + data.word;
    http.begin(dictUrl);
    http.setReuse(false);
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      JsonDocument doc;
      if (deserializeJson(doc, payload) == DeserializationError::Ok && doc.size() > 0 &&
          doc[0]["meanings"].size() > 0 && doc[0]["meanings"][0]["definitions"].size() > 0) {
        JsonObject meaning = doc[0]["meanings"][0];
        data.definition = meaning["definitions"][0]["definition"].as<String>();
        if (meaning["definitions"][0]["example"].is<const char*>()) {
          data.example = meaning["definitions"][0]["example"].as<String>();
        }
        data.success = !data.definition.isEmpty();
      }
    }
    http.end();
    LOG_INF("WOTD", "random '%s' -> dict %d%s", data.word.c_str(), code, data.success ? " ok" : "");
    if (data.success) return data;
    if (pollCancel(cancelInput)) {
      data.cancelled = true;
      return data;
    }
  }
  return WordData{};
}

inline WordData fetchWordOfDay(MappedInputManager* cancelInput = nullptr) {
  WordData data = fetchWiktionaryWotd(cancelInput);
  if (data.cancelled) return data;
  if (!data.success) {
    LOG_INF("WOTD", "Wiktionary WOTD unavailable, falling back to random word");
    data = fetchRandomWordWithDefinition(cancelInput);
  }
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
  LOG_INF("WIKI", "HTTP code: %d", httpCode);
  if (pollCancel(cancelInput)) {
    http.end();
    data.cancelled = true;
    return data;
  }

  // Accept 200 (success) or 303 (redirect that wasn't followed)
  if (httpCode == 200 || httpCode == 303) {
    String payload = http.getString();
    LOG_INF("WIKI", "Payload length: %d", payload.length());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    LOG_INF("WIKI", "JSON parse: %s", err.c_str());

    if (err == DeserializationError::Ok) {
      data.title = doc["title"].as<String>();
      data.extract = doc["extract"].as<String>();

      LOG_INF("WIKI", "Title: %s", data.title.c_str());

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
            if (c == '<') {
              inTag = true;
              continue;
            }
            if (c == '>') {
              inTag = false;
              continue;
            }
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
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();

  // The store hands out copies (std::optional) rather than references now that
  // it guards its strings with a mutex.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  std::optional<WifiCredential> cred = lastSsid.empty() ? std::nullopt : WIFI_STORE.findCredential(lastSsid);
  if (!cred && WIFI_STORE.getCredentialCount() > 0) {
    cred = WIFI_STORE.getCredentialAt(0);
  }
  if (!cred) {
    LOG_ERR("WiFi", "No saved credentials - connect once via Settings > WiFi");
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
  LOG_INF("WiFi", "ensureWiFi: %s (ssid: %s)", WiFi.status() == WL_CONNECTED ? "connected" : "FAILED",
          cred->ssid.c_str());
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace OnlineContentFetcher
