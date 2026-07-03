// FORK: definitions for MySerialImpl.
//
// Upstream declares MySerialImpl (lib/Logging/Logging.h) but no longer defines
// its members, because upstream code has migrated entirely to the LOG_* macros
// and never references `Serial` directly. Fork activities (games / online /
// learning) still use `Serial.printf(...)` etc., which expand to
// MySerialImpl::instance, so we must provide the definitions ourselves.
//
// Kept in a fork-owned translation unit so we never edit upstream's Logging.cpp
// (avoids merge conflicts on every upstream release).

#include <stdarg.h>

#include "Logging.h"

MySerialImpl MySerialImpl::instance;

size_t MySerialImpl::printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buf[256];
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return logSerial.print(buf);
}

size_t MySerialImpl::write(uint8_t b) { return logSerial.write(b); }

size_t MySerialImpl::write(const uint8_t* buffer, size_t size) { return logSerial.write(buffer, size); }

void MySerialImpl::flush() { logSerial.flush(); }
