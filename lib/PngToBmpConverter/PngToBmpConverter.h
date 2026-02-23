#pragma once

class FsFile;
class Print;

class PngToBmpConverter {
 public:
  // Convert PNG to 1-bit BMP (black and white with dithering) for e-ink display
  static bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
};
