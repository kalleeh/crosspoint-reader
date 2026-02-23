#include "Page.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Image path is stored as EPUB-relative, need to resolve to cached BMP
  // For now, try to open as-is (will be updated when caching is integrated)
  FsFile imageFile;
  if (!SdMan.openFileForRead("IMG", image->imagePath, imageFile)) {
    Serial.printf("[%lu] [IMG] Failed to open image: %s\n", millis(), image->imagePath.c_str());
    // Draw placeholder
    renderer.drawText(fontId, xPos + xOffset, yPos + yOffset, "[Image]", true);
    return;
  }

  Bitmap bitmap(imageFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    Serial.printf("[%lu] [IMG] Failed to parse BMP headers: %s\n", millis(), image->imagePath.c_str());
    imageFile.close();
    renderer.drawText(fontId, xPos + xOffset, yPos + yOffset, "[Image]", true);
    return;
  }

  renderer.drawBitmap(bitmap, xPos + xOffset, yPos + yOffset, renderer.getScreenWidth(), renderer.getScreenHeight());
  imageFile.close();
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writeString(file, image->imagePath);
  serialization::writePod(file, image->width);
  serialization::writePod(file, image->height);
  return true;
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  std::string imagePath;
  uint16_t width;
  uint16_t height;

  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readString(file, imagePath);
  serialization::readPod(file, width);
  serialization::readPod(file, height);

  auto img = std::make_shared<ImageBlock>(imagePath, 0, 0, width, height);
  return std::unique_ptr<PageImage>(new PageImage(std::move(img), xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset);
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));
    if (!el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      page->elements.push_back(std::move(pi));
    } else {
      Serial.printf("[%lu] [PGE] Deserialization failed: Unknown tag %u\n", millis(), tag);
      return nullptr;
    }
  }

  return page;
}
