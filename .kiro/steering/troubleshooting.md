---
inclusion: manual
---

# Troubleshooting Guide

Reference this file with `#troubleshooting-guide` when encountering issues.

## Build Issues

### "undefined reference" errors
**Cause**: Missing implementation or incorrect linking
**Solution**: 
- Check that .cpp file exists for .h file
- Verify all methods declared in .h are implemented in .cpp
- Check include paths in platformio.ini

### "incomplete type" errors
**Cause**: Missing includes or forward declarations
**Solution**:
- Add required includes: `<HalDisplay.h>`, `<GfxRenderer.h>`, `../../MappedInputManager.h`
- Check include order (own header first, then system, then project)

### ArduinoJson "proxy non-copyable" errors
**Cause**: Using `auto` with ArduinoJson v7 proxies
**Solution**: Use explicit types
```cpp
// ❌ Wrong
auto current = doc["current_condition"][0];

// ✅ Correct
JsonObject current = doc["current_condition"][0];
```

### Flash/RAM overflow
**Cause**: Firmware too large for ESP32-C3
**Solution**:
- Remove unused features
- Optimize string usage
- Check for large static buffers
- Use `#ifndef OMIT_FONTS` for optional fonts

## Runtime Issues

### Activity stuck on "Loading..."
**Cause**: Async operation not completing
**Solution**:
- Check WiFi connection: `WiFi.status() == WL_CONNECTED`
- Verify `fetchData()` is called in `onEnter()`
- Ensure `render()` is called after state changes
- Check HTTP response codes

### Buttons not responding
**Cause**: Wrong input method or missing input check
**Solution**:
- Use `wasPressed()` for single actions, not `isPressed()`
- Check button enum: `MappedInputManager::Button::Back`
- Verify `loop()` is implemented and called

### Screen not updating
**Cause**: Missing `displayBuffer()` call
**Solution**:
- Always call `renderer.displayBuffer(HalDisplay::FAST_REFRESH)` after drawing
- Check that `render()` is called after state changes

### Memory crashes/reboots
**Cause**: Stack overflow or heap exhaustion
**Solution**:
- Reduce local variable sizes
- Avoid large arrays on stack
- Free allocated memory in `onExit()`
- Check for memory leaks (repeated allocations)

## API Issues

### HTTP requests failing
**Symptoms**: Always getting ERROR state
**Debug steps**:
1. Check WiFi: `Serial.println(WiFi.status());`
2. Check HTTP code: `Serial.println(httpCode);`
3. Check response: `Serial.println(payload);`
4. Verify API endpoint is accessible

### JSON parsing errors
**Symptoms**: `deserializeJson()` returns error
**Debug steps**:
1. Print raw JSON: `Serial.println(payload);`
2. Check JSON structure matches code
3. Verify JsonDocument size is adequate
4. Use explicit types (JsonObject, JsonArray)

### Weather API not working
**Common issues**:
- Using HTTPS instead of HTTP (wttr.in supports both)
- Incorrect JSON path (check API response structure)
- Network timeout (increase timeout if needed)

## Display Issues

### Excessive ghosting
**Cause**: Too many FAST_REFRESH without FULL_REFRESH
**Solution**:
- Add periodic FULL_REFRESH (every 10-20 updates)
- Use FULL_REFRESH for static content
- Design UI to minimize large filled areas

### Text not visible
**Cause**: Wrong color parameter or off-screen coordinates
**Solution**:
- Check color parameter: `true` for black, `false` for white
- Verify x, y coordinates are within screen bounds (0-480, 0-800)
- Check font ID is valid

### Layout issues
**Cause**: Incorrect coordinate calculations
**Solution**:
- Use `renderer.getScreenWidth()` and `getScreenHeight()`
- Account for margins (typically 20px)
- Test with different content lengths

## Navigation Issues

### Back button doesn't work
**Cause**: Missing or incorrect callback
**Solution**:
- Verify `onBack` callback is passed to constructor
- Check `onBack()` is called in `loop()` when Back pressed
- Ensure parent activity is properly registered

### Menu items not appearing
**Cause**: Not registered or incorrect registration
**Solution**:
- Check `registerItem()` is called before `enterNewActivity()`
- Verify item ID and display name are correct
- Check navigation function is implemented

## Common Mistakes

### Using `auto` with ArduinoJson
```cpp
// ❌ Wrong - causes compile error
auto data = doc["key"];

// ✅ Correct
JsonObject data = doc["key"];
String value = doc["key"].as<String>();
```

### Using `isPressed()` for menus
```cpp
// ❌ Wrong - triggers multiple times
if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
  onConfirm();
}

// ✅ Correct - triggers once per press
if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
  onConfirm();
}
```

### Forgetting to render after state change
```cpp
// ❌ Wrong - screen doesn't update
state = LOADED;

// ✅ Correct
state = LOADED;
render();
```

### Not checking WiFi before HTTP
```cpp
// ❌ Wrong - crashes if WiFi disconnected
HTTPClient http;
http.begin(url);

// ✅ Correct
if (WiFi.status() != WL_CONNECTED) {
  state = ERROR;
  render();
  return;
}
HTTPClient http;
http.begin(url);
```

## Debug Tools

### Serial Output
```cpp
Serial.begin(115200);
Serial.println("Debug message");
Serial.printf("Value: %d\n", value);
```

### Memory Usage
Check build output for flash/RAM usage:
```
RAM:   [===       ]  32.5% (used 106508 bytes from 327680 bytes)
Flash: [==========]  95.6% (used 6264368 bytes from 6553600 bytes)
```

### Build Verbose Output
```bash
cd /path/to/crosspoint-reader
pio run -v  # Verbose build output
```

## Getting Help

When reporting issues, include:
1. Build output (flash/RAM usage)
2. Error messages (compile or runtime)
3. Code snippet showing the issue
4. Steps to reproduce
5. Expected vs actual behavior
