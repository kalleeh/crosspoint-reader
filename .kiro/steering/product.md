---
inclusion: always
---

# Crosspoint Reader - Product Overview

## Product Purpose
Crosspoint Reader is an open-source e-reader firmware for the Xteink X4 e-ink device. It transforms a basic e-reader into a versatile device with reading, gaming, and online features optimized for e-ink displays.

## Target Users
- E-reader enthusiasts who want more control over their devices
- Developers interested in e-ink display programming
- Users who want games and online features on their e-reader
- Open-source hardware hackers

## Key Features

### Reading
- EPUB book support with full rendering
- Library management with recent books
- Chapter navigation
- Customizable reading settings
- OPDS server integration for remote libraries

### Games (5 games total)
- Snake - Classic snake game with optimized controls
- Chess - Full chess implementation with AI opponent (depth 1 for ESP32 performance)
- 2048 - Tile-sliding puzzle game
- Tic Tac Toe - Classic two-player game
- Memory Match - Card matching game with symbols

### Online Features (5 features total)
- Weather - Current conditions with custom e-ink weather icons (wttr.in API)
- Wikipedia Random - Browse random Wikipedia article summaries
- Word of the Day - Random words with definitions and examples
- This Day in History - Historical events for today's date
- XKCD Comics - Browse XKCD comics with navigation

## Business Objectives
- Provide a fully open-source alternative to proprietary e-reader firmware
- Demonstrate e-ink display capabilities beyond reading
- Build a community around e-reader customization
- Showcase ESP32 capabilities for embedded applications

## Design Philosophy
- **E-ink First**: All features optimized for e-ink refresh characteristics
- **Button-Only Input**: No touchscreen dependency, full button navigation
- **Performance**: Efficient memory usage (95.6% flash, 32.5% RAM on ESP32-C3)
- **Fast Refresh**: Use FAST_REFRESH mode for interactive features (games, online)
- **Minimal Dependencies**: Leverage built-in ESP32 libraries where possible
