#include "AppsMenuActivity.h"
#include <GfxRenderer.h>
#include "../../MappedInputManager.h"
#include "../../fontIds.h"

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  render();
}

void AppsMenuActivity::render() {
  renderer.clearScreen();
  
  const int width = renderer.getScreenWidth();
  const int margin = 20;
  
  // Title
  renderer.drawText(UI_12_FONT_ID, margin, margin, "Apps", true);
  
  // Menu items
  const char* items[] = {"Online", "Games", "AWS Cert Practice"};
  const int itemHeight = 50;
  const int startY = 80;
  
  for (int i = 0; i < 3; i++) {
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

void AppsMenuActivity::loop() {
  Activity::loop();
  
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      onOnline();
    } else if (selectedIndex == 1) {
      onGames();
    } else {
      onAWSCert();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
             mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex - 1 + 3) % 3;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % 3;
    render();
  }
}
