#include "AWSCertMenuActivity.h"

#include <ForkI18n.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>
#include <ctime>

#include "../../QuizStatsManager.h"
#include "../../util/ButtonNavigator.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

// Exam data, not UI copy: exam codes, levels, audiences and domain weights are
// AWS's own identifiers and stay untranslated in flash.
constexpr AWSCertMenuActivity::CertOption CERTS[] = {
    // Foundational
    {"cloud-practitioner", "Cloud Practitioner", "CLF-C02", 65, 70, "Foundational", "Business, Sales, Technical",
     "Cloud Concepts 24% | Security 30% | Technology 34% | Billing 12%"},
    {"ai-practitioner", "AI Practitioner", "AIF-C01", 65, 70, "Foundational", "Business, Technical, AI/ML interested",
     "AI/ML Fundamentals 20% | Generative AI 24% | Applications 28% | Security 28%"},
    // Associate
    {"sa-associate", "Solutions Architect Associate", "SAA-C03", 65, 72, "Associate",
     "Solutions Architects, 1yr AWS experience",
     "Resilient Arch 26% | High-Performance 24% | Secure Apps 30% | Cost-Optimized 20%"},
    {"developer-associate", "Developer Associate", "DVA-C02", 65, 72, "Associate", "Developers, 1yr AWS development",
     "Development 32% | Security 26% | Deployment 24% | Troubleshooting 18%"},
    {"sysops-associate", "SysOps Administrator Associate", "SOA-C02", 65, 72, "Associate", "SysOps, 1yr AWS operations",
     "Monitoring 20% | Reliability 16% | Deployment 18% | Security 16% | Networking 18% | Cost 12%"},
    // Professional
    {"sa-professional", "Solutions Architect Professional", "SAP-C02", 75, 75, "Professional",
     "Solutions Architects, 2yr AWS experience",
     "Design Solutions 26% | Continuous Improvement 26% | Migration 18% | Cost Control 20% | Security 10%"},
    {"devops-professional", "DevOps Engineer Professional", "DOP-C02", 75, 75, "Professional",
     "DevOps Engineers, 2yr AWS experience",
     "SDLC Automation 22% | Config Mgmt 17% | Monitoring 15% | Policies 10% | Incident Response 14% | Security 22%"},
    // Specialty
    {"security-specialty", "Security Specialty", "SCS-C02", 65, 75, "Specialty", "Security roles, 2yr AWS security",
     "Threat Detection 14% | Security Logging 18% | Infrastructure 20% | Identity 16% | Data Protection 18% | "
     "Incident Response 14%"},
    {"ml-specialty", "Machine Learning Specialty", "MLS-C01", 65, 75, "Specialty",
     "ML Engineers, 1yr AWS ML experience",
     "Data Engineering 20% | Exploratory Analysis 24% | Modeling 36% | ML Implementation 20%"},
    {"database-specialty", "Database Specialty", "DBS-C01", 65, 75, "Specialty", "Database Architects, 2yr AWS DB",
     "Workload Design 26% | Deployment 20% | Management 18% | Monitoring 18% | Security 18%"},
    {"networking-specialty", "Advanced Networking Specialty", "ANS-C01", 65, 75, "Specialty",
     "Network Engineers, 5yr networking", "Network Design 30% | Implementation 26% | Management 20% | Security 24%"},
    {"analytics-specialty", "Data Analytics Specialty", "DAS-C01", 65, 75, "Specialty",
     "Data Analysts, 2yr AWS analytics",
     "Collection 18% | Storage 22% | Processing 24% | Analysis 18% | Visualization 12% | Security 6%"},
    {"sap-specialty", "SAP on AWS Specialty", "PAS-C01", 65, 75, "Specialty", "SAP Architects, SAP + AWS experience",
     "SAP Workloads 30% | Design 28% | Implementation 20% | Operations 22%"},
};
static_assert(sizeof(CERTS) / sizeof(CERTS[0]) == AWSCertMenuActivity::CERT_COUNT, "CERT_COUNT out of sync");

// Marker for a cert whose question file is missing from the SD card.
constexpr const char* MISSING_FILE_MARKER = "[?]";

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AWSCertMenuActivity::onEnter() {
  tab = Tab::Certifications;
  showingInfo = false;
  confirmingClear = false;

  // Row table: built once; buildScreen only hands out pointers into it.
  for (int i = 0; i < CERT_COUNT; i++) {
    const CertOption& cert = CERTS[i];
    snprintf(certSubtitles[i], SUBTITLE_LEN, "%s \xC2\xB7 %s", cert.level, cert.examCode);

    char path[64];
    snprintf(path, sizeof(path), "/aws-quiz/%s.json", cert.id);
    const bool available = Storage.exists(path);
    if (!available) LOG_DBG("QUIZ", "Missing question file: %s", path);

    certItems[i] = fui::ListItem{};
    certItems[i].label = cert.name;
    certItems[i].subtitle = certSubtitles[i];
    certItems[i].actionValue = static_cast<int16_t>(i);
    certItems[i].enabled = available;
    certItems[i].value = available ? nullptr : MISSING_FILE_MARKER;
  }

  clearItem = fui::ListItem{};
  clearItem.actionValue = 0;

  UiTabListActivity::onEnter();
  // Land on the remembered cert row rather than the tab bar.
  activeNav().selected = selectedCert + 1;
}

// ---------------------------------------------------------------------------
// List / tab contract
// ---------------------------------------------------------------------------

int AWSCertMenuActivity::listCount() const {
  if (showingInfo) return 0;
  if (tab == Tab::Certifications) return CERT_COUNT;
  return statsHasData ? 1 : 0;
}

const char* AWSCertMenuActivity::headerTitle() const { return fork_tr(STR_AWS_MENU_TITLE); }

const char* AWSCertMenuActivity::tabLabel(const int index) const {
  return index == static_cast<int>(Tab::Certifications) ? fork_tr(STR_AWS_TAB_CERTS) : fork_tr(STR_AWS_TAB_STATS);
}

void AWSCertMenuActivity::rememberSelectedCert() {
  if (tab != Tab::Certifications) return;
  const int ring = ringPos();
  if (ring > 0 && ring <= CERT_COUNT) selectedCert = ring - 1;
}

void AWSCertMenuActivity::refreshStatsSummary() {
  statsHasData = QuizStatsManager::getInstance().getTotalQuestionsAnswered(CERTS[selectedCert].id) > 0;
}

// Shared tab-switch path for touch taps, Confirm on the tab bar, Left/Right
// and the continuous-hold walk. Any pending destructive confirm is dropped.
void AWSCertMenuActivity::switchTab(const Tab target, const bool landOnTabBar) {
  rememberSelectedCert();
  confirmingClear = false;
  tab = target;
  if (tab == Tab::Stats) refreshStatsSummary();

  auto& n = activeNav();
  if (landOnTabBar || listCount() == 0) {
    n.selected = 0;
  } else {
    n.selected = tab == Tab::Certifications ? selectedCert + 1 : 1;
  }
  n.followOnBuild = true;  // pull the viewport to the landed row
  requestUpdate();
}

void AWSCertMenuActivity::stepTab(const int direction) {
  const bool onTabBar = ringPos() == 0;
  const int next = direction > 0 ? ButtonNavigator::nextIndex(static_cast<int>(tab), TAB_COUNT)
                                 : ButtonNavigator::previousIndex(static_cast<int>(tab), TAB_COUNT);
  switchTab(static_cast<Tab>(next), onTabBar);
}

void AWSCertMenuActivity::onTabAction(const int index) {
  if (index != static_cast<int>(tab)) {
    switchTab(static_cast<Tab>(index), /*landOnTabBar=*/true);
  }
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void AWSCertMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (tab == Tab::Certifications) {
    selectedCert = index;
    app.clearTapFlash();  // leaving this screen
    onSelectCert(CERTS[selectedCert].id);
    return;
  }
  handleClearAction();
}

// Clearing wipes stats for ALL certs: the first Confirm arms, the second
// clears. Back or a tab switch disarms.
void AWSCertMenuActivity::handleClearAction() {
  if (!statsHasData) return;
  if (!confirmingClear) {
    confirmingClear = true;
    requestUpdate();
    return;
  }
  confirmingClear = false;
  QuizStatsManager::getInstance().clearAllStats();
  LOG_DBG("QUIZ", "Cleared all quiz stats");
  refreshStatsSummary();
  activeNav().selected = 0;  // the clear row is gone; focus the tab bar
  app.clearTapFlash();
  requestUpdate();
}

void AWSCertMenuActivity::openInfo() {
  rememberSelectedCert();
  showingInfo = true;
  requestUpdate();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// The Info page owns every input pass while it is shown, so the ring walk
// cannot disturb the remembered cert selection underneath it.
bool AWSCertMenuActivity::handleCustomInput() {
  if (!showingInfo) return false;
  // Back on press-edge: the fork convention (see AppsMenuActivity). Returning
  // to the list must not also let the release edge fire here.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    showingInfo = false;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showingInfo = false;
    onSelectCert(CERTS[selectedCert].id);
  }
  return true;
}

bool AWSCertMenuActivity::handleButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (confirmingClear) {
      confirmingClear = false;  // cancel the pending clear instead of leaving
      requestUpdate();
    } else {
      onBack();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int ring = ringPos();
    if (ring == 0) {
      stepTab(1);
    } else {
      activateIndex(ring - 1);
    }
    return true;
  }

  // Front Left/Right carry screen actions here (Info / tab switch) instead of
  // joining the ring walk; the side buttons still walk it. The "previous" role
  // follows the same orientation flip mapLabels applies to the hint labels.
  const bool swapped = mappedInput.isNavDirectionSwapped();
  const auto infoButton = swapped ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto tabButton = swapped ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  if (mappedInput.wasReleased(infoButton)) {
    if (tab == Tab::Certifications) {
      openInfo();
    } else {
      stepTab(-1);
    }
    return true;
  }
  if (mappedInput.wasReleased(tabButton)) {
    stepTab(1);
    return true;
  }
  // While either front button is held, keep it out of ButtonNavigator's
  // NavNext/NavPrevious continuous-hold detection.
  if (mappedInput.isPressed(MappedInputManager::Button::Left) ||
      mappedInput.isPressed(MappedInputManager::Button::Right)) {
    return true;
  }
  return false;
}

void AWSCertMenuActivity::drawFooter() {
  MappedInputManager::Labels labels;
  if (showingInfo) {
    labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_PRACTICE), "", "");
  } else if (tab == Tab::Stats) {
    labels = mappedInput.mapLabels(tr(STR_BACK), statsHasData ? fork_tr(STR_BTN_CLEAR_ALL) : "", "", "");
  } else {
    labels = mappedInput.mapLabels(tr(STR_BACK), fork_tr(STR_BTN_START), fork_tr(STR_BTN_INFO), fork_tr(STR_BTN_STATS));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// Screen builders (render task)
// ---------------------------------------------------------------------------

// One left-aligned text line taken off the top of the remaining body. Lines
// that no longer fit are skipped rather than drawn into a clipped rect.
void AWSCertMenuActivity::textLine(UiScreen& screen, const char* text, const fui::TextStyle& style,
                                   const int16_t indent, const int16_t gap) {
  if (!text) return;
  const int16_t lh = screen.target().lineHeight(style.font);
  if (screen.body().height < lh) return;
  fui::Rect rect = screen.takeTop(lh, gap);
  rect.x = static_cast<int16_t>(rect.x + indent);
  rect.width = static_cast<int16_t>(rect.width > indent ? rect.width - indent : 0);
  screen.target().text(rect, text, style);
}

void AWSCertMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  if (showingInfo) {
    buildInfoPage(screen);
    return;
  }

  buildTabBar(screen);
  if (tab == Tab::Certifications) {
    buildCertList(screen);
  } else {
    buildStatsTab(screen);
  }
}

void AWSCertMenuActivity::buildCertList(UiScreen& screen) {
  fui::ListProps props;
  props.items = certItems;
  props.count = CERT_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the missing-file marker and the row edge
  // Small bold title over a small subtitle (the RecentBooks pairing). Bold also
  // marks the style caller-owned so Screen::list() keeps it (FONT_SLOT_SMALL is 0).
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncTabListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void AWSCertMenuActivity::buildStatsTab(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::ThemeTokens& theme = screen.theme();
  const int16_t side = static_cast<int16_t>(metrics.contentSidePadding);
  const int16_t indent = theme.spaceLg;
  screen.insetContent(fui::Insets{0, side, 0, side});

  // The clear row is anchored at the bottom first so the text block above can
  // never push it off-screen (landscape has ~350px of body here).
  if (statsHasData) {
    clearItem.label = confirmingClear ? fork_tr(STR_AWS_CLEAR_CONFIRM) : fork_tr(STR_AWS_CLEAR_STATS);
    fui::ListProps props;
    props.items = &clearItem;
    props.count = 1;
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    syncTabListViewport(screen, props);
    const int16_t rowHeight = props.rowHeight > 0 ? props.rowHeight : theme.rowHeight;
    screen.list(props, rowHeight, fui::LayoutAnchor::Bottom);
    screen.spacer(theme.spaceMd, fui::LayoutAnchor::Bottom);
  }

  auto& stats = QuizStatsManager::getInstance();
  const char* certId = CERTS[selectedCert].id;
  fui::TextStyle heading = theme.smallText;
  heading.bold = true;
  const fui::TextStyle& body = theme.smallText;

  textLine(screen, CERTS[selectedCert].name, theme.bodyText, 0, theme.spaceSm);

  snprintf(lineBuf, sizeof(lineBuf), fork_tr(STR_AWS_STREAK), stats.getStreak(certId));
  textLine(screen, lineBuf, body, 0, theme.spaceMd);

  // Overall progress
  textLine(screen, fork_tr(STR_AWS_OVERALL_PROGRESS), heading);
  const int totalQuestions = stats.getTotalQuestionsAnswered(certId);
  snprintf(lineBuf, sizeof(lineBuf), "%s %d", fork_tr(STR_AWS_QUESTIONS_ANSWERED), totalQuestions);
  textLine(screen, lineBuf, body, indent);
  if (totalQuestions > 0) {
    snprintf(lineBuf, sizeof(lineBuf), "%s %d%%", fork_tr(STR_AWS_AVERAGE_SCORE), stats.getAverageScore(certId));
    textLine(screen, lineBuf, body, indent, theme.spaceMd);
  } else {
    textLine(screen, fork_tr(STR_AWS_AVERAGE_SCORE_NA), body, indent, theme.spaceMd);
  }

  // Recent history
  textLine(screen, fork_tr(STR_AWS_RECENT_HISTORY), heading);
  const auto history = stats.getRecentHistory(HISTORY_ROWS, certId);
  if (history.empty()) {
    textLine(screen, fork_tr(STR_AWS_NO_HISTORY), body, indent, theme.spaceMd);
  } else {
    for (size_t i = 0; i < history.size(); i++) {
      const auto& result = history[i];
      // The record is packed: copy the uint32_t into a time_t (64-bit) rather
      // than casting the pointer (unaligned load + over-read).
      const time_t ts = result.timestamp;
      const struct tm* timeinfo = localtime(&ts);
      char dateStr[16];
      if (timeinfo) {
        strftime(dateStr, sizeof(dateStr), "%b %d", timeinfo);
      } else {
        strncpy(dateStr, "----", sizeof(dateStr));
      }
      const int percentage = result.total > 0 ? (result.score * 100) / result.total : 0;
      snprintf(lineBuf, sizeof(lineBuf), "\xE2\x80\xA2 %s: %.*s (%d%%)", dateStr, static_cast<int>(sizeof(result.mode)),
               result.mode, percentage);
      textLine(screen, lineBuf, body, indent, i + 1 == history.size() ? theme.spaceMd : 0);
    }
  }

  // Weak areas (top 3 below the 70% threshold)
  textLine(screen, fork_tr(STR_AWS_WEAK_AREAS), heading);
  const auto weakDomains = stats.getWeakDomains(5, certId);
  if (weakDomains.empty()) {
    textLine(screen, fork_tr(STR_AWS_NO_WEAK_AREAS), body, indent);
  } else {
    int shown = 0;
    for (const auto& pair : weakDomains) {
      if (shown++ >= WEAK_ROWS) break;
      snprintf(lineBuf, sizeof(lineBuf), "\xE2\x80\xA2 %s: %d%%", pair.first.c_str(), pair.second);
      textLine(screen, lineBuf, body, indent);
    }
  }
}

void AWSCertMenuActivity::buildInfoPage(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const fui::ThemeTokens& theme = screen.theme();
  const CertOption& cert = CERTS[selectedCert];
  const int16_t side = static_cast<int16_t>(metrics.contentSidePadding);
  const int16_t indent = theme.spaceLg;
  screen.insetContent(fui::Insets{0, side, 0, side});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::TextStyle heading = theme.smallText;
  heading.bold = true;
  const fui::TextStyle& body = theme.smallText;

  textLine(screen, cert.name, theme.titleText, 0, theme.spaceSm);

  // Level badge: outlined, label-hugging pill.
  {
    fui::TextStyle badgeText = body;
    badgeText.align = fui::TextAlign::Center;
    const int16_t lh = screen.target().lineHeight(badgeText.font);
    const int16_t badgeH = static_cast<int16_t>(lh + theme.spaceSm * 2);
    if (screen.body().height >= badgeH) {
      const fui::Rect band = screen.takeTop(badgeH, theme.spaceMd);
      const int16_t textW = screen.target().measureText(badgeText.font, cert.level, badgeText).width;
      int16_t badgeW = static_cast<int16_t>(textW + theme.spaceLg * 2);
      if (badgeW > band.width) badgeW = band.width;
      const fui::Rect badge{band.x, band.y, badgeW, badgeH};
      screen.target().stroke(badge, fui::Paint::solid(fui::Color::Black), 1, static_cast<uint8_t>(badgeH / 2));
      screen.target().text(badge.inset(fui::Insets{theme.spaceSm, 0, theme.spaceSm, 0}), cert.level, badgeText);
    }
  }

  // Exam facts. "Exam"/"Questions"/"Pass" were literals on the legacy page too.
  snprintf(lineBuf, sizeof(lineBuf), "Exam: %s", cert.examCode);
  textLine(screen, lineBuf, body);
  snprintf(lineBuf, sizeof(lineBuf), "Questions: %u | Pass: %u%%", static_cast<unsigned>(cert.questionCount),
           static_cast<unsigned>(cert.passingScore));
  textLine(screen, lineBuf, body);
  textLine(screen, fork_tr(STR_AWS_EXAM_DURATION), body, 0, theme.spaceMd);

  // Target audience
  textLine(screen, fork_tr(STR_AWS_TARGET_AUDIENCE), heading);
  textLine(screen, cert.audience, body, indent, theme.spaceMd);

  // Domain breakdown: split the flash string on '|' and trim each piece.
  textLine(screen, fork_tr(STR_AWS_EXAM_DOMAINS), heading);
  const char* p = cert.domains;
  while (p && *p) {
    const char* bar = strchr(p, '|');
    size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
    while (len > 0 && *p == ' ') {
      p++;
      len--;
    }
    while (len > 0 && p[len - 1] == ' ') len--;
    snprintf(lineBuf, sizeof(lineBuf), "%.*s", static_cast<int>(len), p);
    textLine(screen, lineBuf, body, indent, bar ? 0 : theme.spaceMd);
    if (!bar) break;
    p = bar + 1;
  }

  fui::TextStyle note = body;
  note.maxLines = 2;
  const int16_t noteH = static_cast<int16_t>(screen.target().lineHeight(note.font) * 2);
  if (screen.body().height >= noteH) {
    screen.target().text(screen.takeTop(noteH), fork_tr(STR_AWS_PRACTICE_INFO), note);
  }
}
