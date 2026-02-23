---
inclusion: fileMatch
fileMatchPattern: ["src/activities/**/*.cpp", "src/activities/**/*.h"]
---

# Activity Development Standards

## Activity Creation Checklist

When creating a new activity:

1. **Create header file** (`ActivityName.h`)
   - Include guard with `#pragma once`
   - Include `../Activity.h` and `<functional>`
   - Define class inheriting from `Activity`
   - Add `onBack` callback member
   - Declare `onEnter()` and `loop()` overrides

2. **Create implementation file** (`ActivityName.cpp`)
   - Include required headers in proper order
   - Implement `onEnter()` - initialization and first render
   - Implement `loop()` - input handling
   - Implement `render()` - UI drawing

3. **Wire up navigation** (in `main.cpp`)
   - Add include for new activity
   - Add forward declaration `void onGoToActivityName();`
   - Implement navigation function
   - Register in parent menu

## Required Includes for Activities

```cpp
// In .cpp file
#include "ActivityName.h"
#include <HalDisplay.h>
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"

// For online features, also add:
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
```

## WiFi Management for Online Activities

**CRITICAL**: WiFi is OFF by default. Online activities MUST enable WiFi in `onEnter()` and disable in `onExit()`.

**ESP32 WiFi Credentials**: Saved automatically to flash when you call `WiFi.begin(ssid, password)`. Calling `WiFi.begin()` without parameters uses saved credentials (much faster!).

```cpp
void OnlineActivity::onEnter() {
  // Enable WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Auto-reconnect to saved network (fast!)
  
  // Wait briefly for connection (up to 2 seconds)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(100);
    attempts++;
  }
  
  // Check if connected with valid IP
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchData();
  } else {
    state = ERROR;
    render();
  }
}

void OnlineActivity::onExit() {
  // Disable WiFi to save power
  WiFi.mode(WIFI_OFF);
}
```

**How WiFi Works**:
1. User connects via Settings → WiFi (credentials saved to flash)
2. Online activities call `WiFi.begin()` to auto-reconnect
3. Connection typically takes 1-2 seconds
4. If connection fails, show ERROR state
5. WiFi disabled on exit to save battery

**Why check IP address?**
- `WiFi.status()` can return `WL_CONNECTED` even without valid connection
- Checking `WiFi.localIP() != IPAddress(0, 0, 0, 0)` ensures real connectivity

## State Management for Async Operations

Activities that fetch data should use this pattern:

```cpp
enum State { LOADING, LOADED, ERROR };
State state = LOADING;

void onEnter() override {
  render();  // Show loading state
  fetchData();  // Fetch and update state
}

void fetchData() {
  state = LOADING;
  render();
  
  if (WiFi.status() != WL_CONNECTED) {
    state = ERROR;
    render();
    return;
  }
  
  // Fetch data...
  
  if (success) {
    state = LOADED;
  } else {
    state = ERROR;
  }
  render();
}
```

## Scrolling Pattern

For activities with scrollable content:

```cpp
int scrollOffset = 0;
int maxScroll = 0;

void render() {
  int y = margin - scrollOffset;
  
  // Draw content, tracking y position
  // ...
  
  maxScroll = max(0, y - height + margin);
}

void loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (scrollOffset > 0) {
      scrollOffset = max(0, scrollOffset - 50);
      render();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (scrollOffset < maxScroll) {
      scrollOffset = min(maxScroll, scrollOffset + 50);
      render();
    }
  }
}
```

## Text Wrapping Pattern

For word-wrapped text display:

```cpp
const int maxWidth = width - 2 * margin;
String remaining = text;

while (remaining.length() > 0 && y < height + scrollOffset) {
  int breakPos = remaining.length();
  
  // Find longest substring that fits
  for (int i = 1; i <= remaining.length(); i++) {
    String test = remaining.substring(0, i);
    if (renderer.getTextWidth(UI_10_FONT_ID, test.c_str()) > maxWidth) {
      breakPos = i - 1;
      break;
    }
  }
  
  // Break at word boundary
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
```

## HTTP Request Pattern

For online features:

```cpp
HTTPClient http;
http.begin("https://api.example.com/endpoint");

int httpCode = http.GET();
if (httpCode == 200) {
  String payload = http.getString();
  JsonDocument doc;
  
  if (deserializeJson(doc, payload) == DeserializationError::Ok) {
    // CRITICAL: Use JsonObject, not auto
    JsonObject data = doc["key"];
    
    // Extract data
    String value = data["field"].as<String>();
    int number = data["number"].as<int>();
    
    state = LOADED;
  } else {
    state = ERROR;
  }
} else {
  state = ERROR;
}

http.end();
render();
```

## Refresh Mode Selection

- **FAST_REFRESH**: Use for all interactive features (games, online, menus)
  - Faster updates (~300ms)
  - Some ghosting acceptable
  - Better user experience for interactive content

- **FULL_REFRESH**: Use only for reading
  - Slower updates (~1-2s)
  - No ghosting
  - Better for static content

## Memory Efficiency

- Avoid large static buffers
- Use `String` for dynamic text (Arduino String class)
- Free resources in `onExit()` if allocated
- Reuse buffers where possible
- Keep stack usage minimal (ESP32-C3 has limited stack)

## Error Handling

Always handle these error cases:
- WiFi not connected (check `WiFi.status() != WL_CONNECTED`)
- HTTP request failures (check `httpCode != 200`)
- JSON parsing errors (check `deserializeJson()` result)
- Display appropriate error messages to user

## Testing Checklist

Before committing new activities:
- ✅ Builds without errors (`pio run`)
- ✅ Flash usage under 96% (check build output)
- ✅ All buttons work correctly
- ✅ Back button returns to parent menu
- ✅ No memory leaks (test multiple enter/exit cycles)
- ✅ Error states display properly
- ✅ Loading states show immediately
