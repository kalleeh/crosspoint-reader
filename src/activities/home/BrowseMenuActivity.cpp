#include "BrowseMenuActivity.h"
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"

void BrowseMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  render();
}

void BrowseMenuActivity::render() {
  renderer.clearScreen();
  
  const int width = renderer.getScreenWidth();
  const int margin = 20;
  
  // Title
  renderer.drawText(UI_12_FONT_ID, margin, margin, "Browse", true);
  
  // Menu items
  const char* items[] = {"OPDS Browser", "File Transfer"};
  const int itemHeight = 50;
  const int startY = 80;
  
  for (int i = 0; i < 2; i++) {
    const int y = startY + i * (itemHeight + 10);
    const bool selected = (i == selectedIndex);
    
    if (selected) {
      renderer.fillRect(margin, y, width - 2 * margin, itemHeight);
    } else {
      renderer.drawRect(margin, y, width - 2 * margin, itemHeight);
    }
    
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, items[i]);
    const int textX = (width - textWidth) / 2;
    const int textY = y + (itemHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, items[i], !selected);
  }
  
  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BrowseMenuActivity::loop() {
  Activity::loop();
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      onOpdsBrowser();
    } else {
      onFileTransfer();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
             mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex - 1 + 2) % 2;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % 2;
    render();
  }
}
