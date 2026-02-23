---
inclusion: always
---

# Crosspoint Reader - Technology Stack

## Hardware Platform
- **Device**: Xteink X4 e-reader
- **MCU**: ESP32-C3 (RISC-V single-core)
- **RAM**: 128MB total (~380KB usable for application)
- **Flash**: 6.55MB available for firmware
- **Display**: 480×800 e-ink display, 220 PPI, 16 grayscale levels
- **Input**: 6 physical buttons (2× page turn, Back, Confirm, 2× front configurable)
- **Connectivity**: WiFi 2.4GHz only
- **Storage**: SD card for books and data

## Core Framework
- **Platform**: PlatformIO (ESP32 Arduino framework)
- **Build System**: PlatformIO CLI (`pio run`)
- **Language**: C++17
- **Standard Library**: Arduino core for ESP32

## Key Libraries

### Display & Graphics
- **HalDisplay**: Hardware abstraction for e-ink display
  - Supports FAST_REFRESH (for interactive features) and FULL_REFRESH modes
  - Buffer-based rendering (480×800 pixels)
- **GfxRenderer**: Graphics rendering engine
  - Text rendering with multiple font sizes (UI_10, UI_12, BOOKERLY_14/16)
  - Primitive drawing (lines, rectangles, circles, fillRect, drawRect)
  - Screen buffer management

### Input Management
- **MappedInputManager**: Button input abstraction
  - Buttons: Up, Down, Left, Right, Back, Confirm
  - Methods: `wasPressed()`, `wasReleased()`, `isPressed()`
  - Use `wasPressed()` for single-press actions (menus, navigation)
  - Use `isPressed()` only for continuous input (not recommended for most cases)

### Networking
- **WiFi**: ESP32 built-in WiFi library
- **HTTPClient**: ESP32 built-in HTTP client for API calls
- **ArduinoJson v7**: JSON parsing and serialization
  - **CRITICAL**: Use `JsonObject` instead of `auto` to avoid proxy copy errors
  - Example: `JsonObject current = doc["current_condition"][0];`

### File System
- **SDCardManager**: SD card access abstraction
- **Epub**: EPUB book parsing and rendering
- **Bitmap**: BMP image loading for covers

### Storage
- **CrossPointSettings**: Persistent settings storage
- **RecentBooksStore**: Recent books tracking
- **KOReaderCredentialStore**: Credential management

## Development Tools
- **PlatformIO**: Build and dependency management
- **esptool.py**: Firmware flashing tool
- **Git**: Version control with submodules

## API Services (All Free, No Keys Required)
- **wttr.in**: Weather data (JSON format)
- **Wikipedia REST API**: Random articles, "On This Day" events
- **Random Word API**: Word generation
- **Dictionary API**: Word definitions and examples
- **XKCD JSON API**: Comic metadata

## Memory Constraints
- **Flash Usage**: Currently at 95.6% (6.26MB / 6.55MB)
- **RAM Usage**: 32.5% (106KB / 327KB)
- **Optimization Priority**: Minimize code size, avoid large static buffers
- **String Handling**: Use Arduino `String` class, avoid std::string where possible

## Build Configuration
- **Target**: ESP32-C3
- **Framework**: Arduino
- **Build Command**: `cd /path/to/crosspoint-reader && pio run`
- **Firmware Output**: `.pio/build/default/firmware.bin`
- **Flash Method**: Copy firmware.bin to device via USB/OTA
