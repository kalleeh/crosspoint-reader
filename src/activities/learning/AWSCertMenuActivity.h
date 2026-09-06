#pragma once

#include <GfxRenderer.h>

#include "../../MappedInputManager.h"
#include "../UiTabListActivity.h"

// AWS certification hub: a two-tab FreeInkUI list screen (Certifications /
// Stats) plus an in-activity Info page for the selected certification.
// Ring model (UiTabListActivity): 0 = tab bar, 1..N = rows.
class AWSCertMenuActivity final : public UiTabListActivity {
 public:
  struct CertOption {
    const char* id;
    const char* name;
    const char* examCode;
    uint8_t questionCount;
    uint8_t passingScore;
    const char* level;
    const char* audience;
    const char* domains;  // '|'-separated
  };

  static constexpr int CERT_COUNT = 13;

  explicit AWSCertMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*onBack)(),
                               void (*onSelectCert)(const char*))
      : UiTabListActivity("AWS Certifications", renderer, mappedInput), onBack(onBack), onSelectCert(onSelectCert) {}

  void onEnter() override;

 private:
  enum class Tab : int { Certifications = 0, Stats = 1 };
  static constexpr int TAB_COUNT = 2;
  static constexpr size_t SUBTITLE_LEN = 32;
  static constexpr int HISTORY_ROWS = 5;
  static constexpr int WEAK_ROWS = 3;

  void (*const onBack)();
  void (*const onSelectCert)(const char*);

  Tab tab = Tab::Certifications;
  int selectedCert = 0;  // last cert row the user rested on; drives Info + Stats
  bool showingInfo = false;
  bool confirmingClear = false;
  bool statsHasData = false;  // cached: the Stats tab has a "Clear All Stats" row

  // Row storage for the Certifications tab. Labels point at flash literals;
  // subtitles ("<level> · <exam code>") are built once in onEnter.
  freeink::ui::ListItem certItems[CERT_COUNT];
  char certSubtitles[CERT_COUNT][SUBTITLE_LEN];
  // Single row of the Stats tab (the clear action); label is a flash string.
  freeink::ui::ListItem clearItem;
  // Scratch for one text line; drawn immediately, so one buffer suffices.
  char lineBuf[128];

  // --- UiListActivity / UiTabListActivity contract ---
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  int tabCount() const override { return TAB_COUNT; }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  // --- screen builders (render task) ---
  void buildCertList(UiScreen& screen);
  void buildStatsTab(UiScreen& screen);
  void buildInfoPage(UiScreen& screen);
  void textLine(UiScreen& screen, const char* text, const freeink::ui::TextStyle& style, int16_t indent = 0,
                int16_t gap = 0);

  // --- state helpers (loop task) ---
  void switchTab(Tab target, bool landOnTabBar);
  void rememberSelectedCert();
  void refreshStatsSummary();
  void openInfo();
  void handleClearAction();
};
