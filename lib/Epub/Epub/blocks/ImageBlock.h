#pragma once

#include <cstdint>
#include <string>

class ImageBlock {
 public:
  std::string imagePath;  // Path to cached BMP image
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;

  ImageBlock(const std::string& imagePath, const int16_t x, const int16_t y, const uint16_t width,
             const uint16_t height)
      : imagePath(imagePath), x(x), y(y), width(width), height(height) {}
};
