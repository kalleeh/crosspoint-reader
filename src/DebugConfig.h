#pragma once

// Debug logging configuration
// Set to 0 to disable all debug logging and save ~15-20KB flash
#ifndef DEBUG_LOGGING
  #define DEBUG_LOGGING 0
#endif

#if DEBUG_LOGGING
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// Always-on logging for critical errors
#define ERROR_PRINT(x) Serial.print(x)
#define ERROR_PRINTLN(x) Serial.println(x)
#define ERROR_PRINTF(...) Serial.printf(__VA_ARGS__)
