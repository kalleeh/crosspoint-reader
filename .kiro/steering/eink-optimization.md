---
inclusion: always
---

# E-ink Display Optimization

## Display Characteristics

### Xteink X4 Specifications
- **Resolution**: 480×800 pixels
- **PPI**: 220 (high quality for e-ink)
- **Grayscale**: 16 levels
- **Refresh Modes**: FAST_REFRESH (~300ms), FULL_REFRESH (~1-2s)
- **No Backlight**: Relies on ambient light
- **No Touchscreen**: Button-only input

## Refresh Mode Guidelines

### FAST_REFRESH (Partial Refresh)
**Use for:**
- Games (Snake, Chess, 2048, TicTacToe, Memory Match)
- Online features (Weather, Wikipedia, Word of Day, History, XKCD)
- Menus and navigation
- Any interactive content requiring frequent updates

**Characteristics:**
- ~300ms refresh time
- Some ghosting acceptable
- Better user experience for interactive features
- Lower power consumption

**Code:**
```cpp
renderer.displayBuffer(HalDisplay::FAST_REFRESH);
```

### FULL_REFRESH (Full Screen Refresh)
**Use for:**
- Book reading (page turns)
- Static content that needs perfect clarity
- Periodic refresh to clear ghosting (every 10-20 partial refreshes)

**Characteristics:**
- ~1-2s refresh time
- No ghosting, perfect clarity
- Higher power consumption
- Noticeable flash

**Code:**
```cpp
renderer.displayBuffer(HalDisplay::FULL_REFRESH);
```

## Graphics Optimization

### Drawing Primitives
- **Lines**: Use for borders, separators
- **Rectangles**: Use for buttons, cards, containers
- **Filled Rectangles**: Use for selected states, backgrounds
- **Circles**: Use for icons, decorative elements

### Text Rendering
- **Available Fonts**: UI_10_FONT_ID, UI_12_FONT_ID, BOOKERLY_14_FONT_ID, BOOKERLY_16_FONT_ID
- **Font Selection**:
  - UI_10: Body text, descriptions, small labels
  - UI_12: Headers, titles, emphasis
  - BOOKERLY_14/16: Book reading (serif font)

### Color/Grayscale Usage
- **Black (true)**: Primary text, borders, filled elements
- **White (false)**: Inverted text, backgrounds
- E-ink displays 16 grayscale levels, but most rendering uses binary (black/white)

## Layout Best Practices

### Margins and Spacing
- **Screen margins**: 20px minimum from edges
- **Line spacing**: 2-5px between text lines
- **Element spacing**: 8-15px between UI elements
- **Bottom margin**: 60px for button legends

### Button Legends
Always show available actions at bottom of screen:
```cpp
// Example: "Back | Confirm: Refresh"
const char* legend = "Back | Confirm: Action";
renderer.drawText(UI_10_FONT_ID, margin, height - 40, legend, true);
```

### Contrast and Readability
- Use high contrast (black on white or white on black)
- Avoid thin lines (minimum 2px width)
- Use adequate font sizes (minimum UI_10)
- Ensure text doesn't touch borders

## Performance Optimization

### Minimize Redraws
- Only call `render()` when state changes
- Batch multiple changes before rendering
- Use dirty regions if possible (not currently implemented)

### Buffer Management
- Screen buffer is 480×800 pixels (large!)
- Avoid multiple full-screen clears
- Reuse buffer content when possible

### Memory Considerations
- Each pixel uses memory
- Complex graphics increase RAM usage
- Keep rendering code efficient

## E-ink Specific Patterns

### Loading States
Show immediate feedback:
```cpp
void onEnter() {
  state = LOADING;
  render();  // Show "Loading..." immediately
  fetchData();  // Then fetch
}
```

### Progressive Rendering
For long content, render visible portion first:
```cpp
// Only render content within viewport
if (y >= 0 && y < height) {
  renderer.drawText(UI_10_FONT_ID, x, y, text, true);
}
```

### Ghosting Management
- Accept some ghosting for interactive features
- Use FULL_REFRESH periodically if needed
- Design UI to minimize ghosting impact (avoid large filled areas)

## Icon Design for E-ink

### Custom Icons (like Weather)
- Use geometric primitives (circles, lines, arcs)
- Keep designs simple and bold
- Ensure good contrast
- Test at target size

### Example: Weather Icons
```cpp
// Sun: Circle with rays
renderer.drawCircle(centerX, centerY, radius);
for (int i = 0; i < 8; i++) {
  // Draw rays at 45° intervals
}

// Cloud: Multiple overlapping circles
renderer.drawCircle(x1, y, r1);
renderer.drawCircle(x2, y, r2);
renderer.drawCircle(x3, y, r3);
```

## Anti-Patterns for E-ink

❌ **Don't:**
- Use animations (e-ink is too slow)
- Display photos/images (limited grayscale, slow refresh)
- Use gradients (limited grayscale levels)
- Update too frequently (causes flashing)
- Use very small fonts (hard to read)
- Rely on color (monochrome display)

✅ **Do:**
- Use clear, high-contrast designs
- Provide immediate visual feedback
- Design for button navigation
- Use text and simple graphics
- Optimize for readability
- Accept e-ink limitations

## Testing on Device

When testing new features:
1. Check readability in different lighting
2. Verify button navigation works smoothly
3. Ensure refresh rate feels responsive
4. Check for excessive ghosting
5. Test battery impact (frequent refreshes drain battery)
