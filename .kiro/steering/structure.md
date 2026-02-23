---
inclusion: always
---

# Crosspoint Reader - Project Structure

## Directory Organization

```
crosspoint-reader/
├── src/
│   ├── main.cpp                    # Entry point, navigation setup
│   ├── MappedInputManager.h/.cpp   # Button input handling
│   ├── CrossPointSettings.h        # Settings management
│   ├── CrossPointState.h           # Application state
│   ├── fontIds.h                   # Font ID constants
│   │
│   └── activities/                 # All UI screens/activities
│       ├── Activity.h              # Base activity class
│       ├── home/                   # Home screen
│       │   ├── HomeActivity.h/.cpp
│       │   └── MyLibraryActivity.h/.cpp
│       ├── reader/                 # Book reading
│       │   └── ReaderActivity.h/.cpp
│       ├── games/                  # Game activities
│       │   ├── GamesMenuActivity.h/.cpp
│       │   ├── SnakeActivity.h/.cpp
│       │   ├── ChessActivity.h/.cpp
│       │   ├── Game2048Activity.h/.cpp
│       │   ├── TicTacToeActivity.h/.cpp
│       │   └── MemoryMatchActivity.h/.cpp
│       └── online/                 # Online features
│           ├── OnlineMenuActivity.h/.cpp
│           ├── WeatherActivity.h/.cpp
│           ├── WikipediaRandomActivity.h/.cpp
│           ├── WordOfTheDayActivity.h/.cpp
│           ├── HistoryTodayActivity.h/.cpp
│           └── XKCDViewerActivity.h/.cpp
│
├── lib/                            # Hardware abstraction layer
│   └── hal/
│       ├── HalDisplay.h            # Display interface
│       └── GfxRenderer.h           # Graphics rendering
│
├── platformio.ini                  # PlatformIO configuration
└── README.md                       # Project documentation
```

## Naming Conventions

### Files
- **Activities**: `FeatureNameActivity.h` and `.cpp`
- **Headers**: Match implementation file name
- **Managers**: `FeatureManager.h/.cpp`

### Classes
- **Activities**: `FeatureNameActivity` (inherits from `Activity`)
- **Managers**: `FeatureManager`
- **Utilities**: Descriptive names (e.g., `MappedInputManager`)

### Functions
- **Navigation**: `onGoToFeature()` pattern
- **Callbacks**: `onEventName()` pattern (e.g., `onBack`, `onConfirm`)
- **Private methods**: camelCase (e.g., `fetchWeather()`, `render()`)

### Variables
- **Member variables**: camelCase (e.g., `currentComic`, `scrollOffset`)
- **Constants**: UPPER_SNAKE_CASE (e.g., `UI_10_FONT_ID`)
- **Enums**: PascalCase values (e.g., `State::LOADING`)

## Activity Pattern

All interactive screens inherit from `Activity` base class:

```cpp
class FeatureActivity final : public Activity {
  const std::function<void()> onBack;
  
  void render();
  
 public:
  explicit FeatureActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const std::function<void()>& onBack)
      : Activity("Feature Name", renderer, mappedInput), onBack(onBack) {}
  
  void onEnter() override;
  void loop() override;
};
```

### Activity Lifecycle
1. **onEnter()**: Called when activity starts (initialize, fetch data, render)
2. **loop()**: Called repeatedly (handle input, update state)
3. **onExit()**: Called when activity ends (cleanup)

## Navigation Pattern

Navigation is handled via function callbacks in `main.cpp`:

```cpp
// Forward declaration
void onGoToFeature();

// Implementation
void onGoToFeature() {
  exitActivity();
  enterNewActivity(new FeatureActivity(renderer, mappedInput, onGoBack));
}

// Registration in parent menu
menu->registerItem("id", "Display Name", onGoToFeature);
```

## Menu Pattern

Menus use a registration system:

```cpp
auto* menu = new MenuActivity(renderer, mappedInput, onBack);
menu->registerItem("item-id", "Display Name", onGoToItem);
enterNewActivity(menu);
```

## Rendering Pattern

All activities follow this rendering approach:

```cpp
void render() {
  renderer.clearScreen();
  
  // Draw UI elements
  renderer.drawText(UI_10_FONT_ID, x, y, "Text", true);
  renderer.drawRect(x, y, width, height);
  renderer.fillRect(x, y, width, height);
  
  // Display buffer with appropriate refresh mode
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);  // For interactive features
  // or
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);  // For reading
}
```

## Input Handling Pattern

```cpp
void loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Handle confirm action
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    // Handle up navigation
  }
  // ... other buttons
}
```

## State Management Pattern

Activities with loading states use enums:

```cpp
enum State { LOADING, LOADED, ERROR };
State state = LOADING;

void fetchData() {
  state = LOADING;
  render();
  
  // Fetch data...
  
  if (success) {
    state = LOADED;
  } else {
    state = ERROR;
  }
  render();
}
```

## Include Order
1. Own header file
2. System/Arduino headers (`<Arduino.h>`, `<WiFi.h>`)
3. Library headers (`<ArduinoJson.h>`, `<HTTPClient.h>`)
4. HAL headers (`<HalDisplay.h>`, `<GfxRenderer.h>`)
5. Project headers (`../../MappedInputManager.h`, `../../fontIds.h`)

## Build Artifacts
- **Firmware**: `.pio/build/default/firmware.bin`
- **Named copies**: `crosspoint-{feature}-firmware.bin` in project root
- **Build logs**: PlatformIO output shows flash/RAM usage

## Anti-Patterns to Avoid
- ❌ Don't use `auto` with ArduinoJson v7 (causes proxy copy errors)
- ❌ Don't use `isPressed()` for single-press actions (causes repeated triggers)
- ❌ Don't forget to call `render()` after state changes
- ❌ Don't use FULL_REFRESH for interactive features (too slow)
- ❌ Don't allocate large buffers on stack (limited RAM)
