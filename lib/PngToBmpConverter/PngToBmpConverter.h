#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
  static bool pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         bool oneBit, bool crop = true, uint8_t* inflateRingBuffer = nullptr);

 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true);
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // inflateRingBuffer, if non-null, must point to at least InflateReader::kRingBufferSize
  // (32KB) bytes and is reused as-is instead of malloc'd — see InflateReader::init() for why
  // callers mixing this with networking may need to pre-allocate it.
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                             bool crop = true, uint8_t* inflateRingBuffer = nullptr);
};
