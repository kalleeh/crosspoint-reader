#include "ReadingStatsActivity.h"

#include <ForkI18n.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <ctime>

#include "../../MappedInputManager.h"
#include "../../ReadingStatsManager.h"
#include "../../fontIds.h"
#include "components/UITheme.h"

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  render();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack) {
      onBack();
    }
  }
}

void ReadingStatsActivity::render() {
  renderer.clearScreen();

  const int width = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int margin = 20;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, fork_tr(STR_RSTATS_TITLE));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int lineStep = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  char buf[64];

  auto& stats = READING_STATS;
  const uint32_t totalSec = stats.getTotalSeconds();

  if (totalSec == 0 && stats.getTotalPageTurns() == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, fork_tr(STR_RSTATS_NO_DATA));
  } else {
    // Today
    const auto today = stats.getToday();
    renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_RSTATS_TODAY), true);
    y += lineStep + 4;
    snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_TIME_FMT), (unsigned long)(today.seconds / 3600),
             (unsigned long)((today.seconds % 3600) / 60));
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
    y += lineStep;
    snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_PAGES_FMT), (unsigned long)today.pageTurns);
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
    y += lineStep + 12;

    // All time
    renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_RSTATS_TOTAL), true);
    y += lineStep + 4;
    snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_TIME_FMT), (unsigned long)(totalSec / 3600),
             (unsigned long)((totalSec % 3600) / 60));
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
    y += lineStep;
    snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_PAGES_FMT), (unsigned long)stats.getTotalPageTurns());
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
    y += lineStep;
    snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_SESSIONS_FMT), (unsigned)stats.getTotalSessions());
    renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
    y += lineStep;
    if (totalSec >= 600) {  // only meaningful after 10+ minutes of reading
      const int pagesPerHour = (int)((uint64_t)stats.getTotalPageTurns() * 3600 / totalSec);
      snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_PAGES_PER_HOUR_FMT), pagesPerHour);
      renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
      y += lineStep;
    }
    y += 12;

    // Recent days (needs a synced clock to have been available at record time)
    ReadingStatsManager::DailyBucket days[ReadingStatsManager::NUM_DAILY_BUCKETS];
    const int dayCount = stats.getRecentDays(days, 7);
    if (dayCount > 0) {
      renderer.drawText(UI_12_FONT_ID, margin, y, fork_tr(STR_RSTATS_LAST_DAYS), true);
      y += lineStep + 4;
      const time_t now = time(nullptr);
      const uint16_t todayDays = now > 1000000000L ? (uint16_t)(now / 86400) : 0;
      for (int i = 0; i < dayCount; i++) {
        const int agoDays = todayDays >= days[i].daysSinceEpoch ? todayDays - days[i].daysSinceEpoch : 0;
        snprintf(buf, sizeof(buf), fork_tr(STR_RSTATS_DAY_ROW_FMT), agoDays, days[i].seconds / 60,
                 days[i].pageTurns);
        renderer.drawText(UI_10_FONT_ID, margin + 10, y, buf, true);
        y += lineStep;
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
