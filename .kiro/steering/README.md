# Crosspoint Reader - Kiro Steering Files

This directory contains steering files that provide Kiro with persistent knowledge about the Crosspoint Reader project.

## Available Steering Files

### Always Included (Foundational)

1. **product.md** - Product overview, features, and design philosophy
2. **tech.md** - Technology stack, libraries, and hardware specifications
3. **structure.md** - Project structure, naming conventions, and code patterns
4. **eink-optimization.md** - E-ink display optimization guidelines

### Conditionally Included

5. **activity-standards.md** - Activity development standards (included when working with activity files)
   - Matches: `src/activities/**/*.cpp` and `src/activities/**/*.h`

### Manual Inclusion

6. **troubleshooting.md** - Troubleshooting guide for common issues
   - Reference with: `#troubleshooting-guide`

## Quick Reference

### Creating New Activities
1. Check `activity-standards.md` for the complete checklist
2. Follow patterns in `structure.md`
3. Use refresh modes from `eink-optimization.md`

### Debugging Issues
Reference `#troubleshooting-guide` for:
- Build errors
- Runtime issues
- API problems
- Display issues

### Key Technical Details
- **Device**: Xteink X4 (ESP32-C3, 480×800 e-ink, button-only)
- **Flash**: 95.6% used (6.26MB / 6.55MB)
- **RAM**: 32.5% used (106KB / 327KB)
- **Refresh**: Use FAST_REFRESH for interactive features
- **JSON**: Always use `JsonObject`, never `auto` (ArduinoJson v7)
- **Input**: Use `wasPressed()` for single actions

### Current Features
- **Reading**: EPUB support, library management
- **Games**: Snake, Chess, 2048, TicTacToe, Memory Match
- **Online**: Weather, Wikipedia, Word of Day, History, XKCD

## Maintenance

These steering files should be updated when:
- New features are added
- Architecture changes
- New patterns emerge
- Common issues are discovered
- Technology stack changes

Treat steering file updates like code changes - review and test them.
