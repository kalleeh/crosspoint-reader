#include "PngToBmpConverter.h"

#include <HardwareSerial.h>
#include <PNGdec.h>
#include <SDCardManager.h>

namespace {
// Static variables for PNG callbacks
FsFile* s_pngFile = nullptr;
uint8_t* s_allPixelData = nullptr;  // Single buffer for all pixel data
int s_targetWidth = 0;
int s_targetHeight = 0;
uint8_t* s_lineBuffer = nullptr;
int s_currentLine = 0;
int s_rowSize = 0;

void* pngOpen(const char* filename, int32_t* size) {
  if (s_pngFile && *s_pngFile) {
    *size = s_pngFile->fileSize();
    s_pngFile->rewind();
    return s_pngFile;
  }
  return nullptr;
}

void pngClose(void* handle) {}

int32_t pngRead(PNGFILE* handle, uint8_t* buffer, int32_t length) {
  if (s_pngFile) {
    return s_pngFile->read(buffer, length);
  }
  return 0;
}

int32_t pngSeek(PNGFILE* handle, int32_t position) {
  if (s_pngFile) {
    return s_pngFile->seek(position);
  }
  return 0;
}

// PNG draw callback - write to memory buffer
int pngDraw(PNGDRAW* pDraw) {
  if (!pDraw || !s_lineBuffer || !s_allPixelData) return 0;
  
  const int y = pDraw->y;
  if (y >= s_targetHeight || y < 0) return 1;
  
  const int width = pDraw->iWidth;
  const uint8_t* pixels = pDraw->pPixels;
  if (!pixels) return 0;
  
  // Clear line buffer
  memset(s_lineBuffer, 0, s_rowSize);
  
  // Convert to 1-bit
  for (int x = 0; x < width && x < s_targetWidth; x++) {
    int gray;
    
    if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR) {
      const int idx = x * 3;
      gray = (pixels[idx] * 30 + pixels[idx + 1] * 59 + pixels[idx + 2] * 11) / 100;
    } else if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
      const int idx = x * 4;
      gray = (pixels[idx] * 30 + pixels[idx + 1] * 59 + pixels[idx + 2] * 11) / 100;
    } else {
      gray = pixels[x];
    }
    
    if (gray > 128) {
      const int byteIndex = x / 8;
      const int bitIndex = 7 - (x % 8);
      if (byteIndex < s_rowSize) {
        s_lineBuffer[byteIndex] |= (1 << bitIndex);
      }
    }
  }
  
  // Copy to pixel data buffer
  memcpy(s_allPixelData + (y * s_rowSize), s_lineBuffer, s_rowSize);
  s_currentLine++;
  return 1;
}
}  // namespace

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, const int targetMaxWidth,
                                                        const int targetMaxHeight) {
  PNG png;
  
  s_pngFile = &pngFile;
  s_targetWidth = targetMaxWidth;
  s_targetHeight = targetMaxHeight;
  s_currentLine = 0;
  
  int rc = png.open(nullptr, pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[%lu] [PNG] Failed to open PNG: %d\n", millis(), rc);
    return false;
  }
  
  const int width = png.getWidth();
  const int height = png.getHeight();
  
  int32_t outWidth = (width > targetMaxWidth) ? targetMaxWidth : width;
  int32_t outHeight = (height > targetMaxHeight) ? targetMaxHeight : height;

  // Update targets so pngDraw callback uses clamped dimensions
  s_targetWidth = outWidth;
  s_targetHeight = outHeight;

  s_rowSize = ((outWidth + 31) / 32) * 4;
  const int32_t imageSize = s_rowSize * outHeight;
  
  // Check memory requirements - limit to small images
  if (imageSize > 50000) {  // ~50KB max
    Serial.printf("[%lu] [PNG] Image too large: %d bytes needed\n", millis(), imageSize);
    png.close();
    return false;
  }
  
  // Allocate buffers
  s_lineBuffer = new uint8_t[s_rowSize];
  s_allPixelData = new uint8_t[imageSize];
  
  if (!s_lineBuffer || !s_allPixelData) {
    Serial.printf("[%lu] [PNG] Failed to allocate buffers\n", millis());
    delete[] s_lineBuffer;
    delete[] s_allPixelData;
    s_lineBuffer = nullptr;
    s_allPixelData = nullptr;
    png.close();
    return false;
  }
  
  memset(s_allPixelData, 0, imageSize);
  
  // Decode PNG
  rc = png.decode(nullptr, 0);
  
  const bool decodeSuccess = (rc == PNG_SUCCESS);
  
  png.close();
  delete[] s_lineBuffer;
  s_lineBuffer = nullptr;
  
  if (!decodeSuccess) {
    Serial.printf("[%lu] [PNG] Failed to decode PNG: %d\n", millis(), rc);
    delete[] s_allPixelData;
    s_allPixelData = nullptr;
    return false;
  }
  
  Serial.printf("[%lu] [PNG] Decoded %d lines\n", millis(), s_currentLine);
  
  // Write BMP header
  const int32_t fileSize = 14 + 40 + 8 + imageSize;
  bmpOut.write('B');
  bmpOut.write('M');
  bmpOut.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  const uint16_t reserved = 0;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&reserved), 2);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&reserved), 2);
  const uint32_t dataOffset = 62;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);
  
  const uint32_t dibSize = 40;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&dibSize), 4);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&outWidth), 4);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&outHeight), 4);
  const uint16_t planes = 1;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  const uint16_t bpp = 1;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&bpp), 2);
  const uint32_t compression = 0;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  const uint32_t ppm = 2835;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&ppm), 4);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&ppm), 4);
  const uint32_t colors = 2;
  bmpOut.write(reinterpret_cast<const uint8_t*>(&colors), 4);
  bmpOut.write(reinterpret_cast<const uint8_t*>(&colors), 4);
  
  const uint8_t black[] = {0, 0, 0, 0};
  const uint8_t white[] = {255, 255, 255, 0};
  bmpOut.write(black, 4);
  bmpOut.write(white, 4);
  
  // Write pixel data in reverse order (BMP is bottom-up)
  for (int i = outHeight - 1; i >= 0; i--) {
    bmpOut.write(s_allPixelData + (i * s_rowSize), s_rowSize);
  }
  
  delete[] s_allPixelData;
  s_allPixelData = nullptr;
  
  Serial.printf("[%lu] [PNG] Converted PNG to 1-bit BMP: %dx%d\n", millis(), outWidth, outHeight);
  return true;
}
