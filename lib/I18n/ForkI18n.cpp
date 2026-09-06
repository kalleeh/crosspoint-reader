#include "ForkI18n.h"

#include "ForkI18nStrings.h"
#include "I18n.h"

using namespace fork_i18n_strings;

// Language order matches Language enum in I18nKeys.h:
//   0=EN 1=ES 2=FR 3=DE 4=CZ 5=PO 6=RU 7=SV 8=RO 9=CA 10=UK 11=BE
static const char* const* const FORK_LANG_ARRAYS[] = {
    FORK_STRINGS_EN,  // Language::ENGLISH
    FORK_STRINGS_ES,  // Language::SPANISH
    FORK_STRINGS_FR,  // Language::FRENCH
    FORK_STRINGS_DE,  // Language::GERMAN
    FORK_STRINGS_CZ,  // Language::CZECH
    FORK_STRINGS_PO,  // Language::PORTUGUESE
    FORK_STRINGS_RU,  // Language::RUSSIAN
    FORK_STRINGS_SV,  // Language::SWEDISH
    FORK_STRINGS_RO,  // Language::ROMANIAN
    FORK_STRINGS_CA,  // Language::CATALAN
    FORK_STRINGS_UK,  // Language::UKRAINIAN
    FORK_STRINGS_BE,  // Language::BELARUSIAN
};

static constexpr int FORK_LANG_COUNT = static_cast<int>(sizeof(FORK_LANG_ARRAYS) / sizeof(FORK_LANG_ARRAYS[0]));

ForkI18n& ForkI18n::getInstance() {
  static ForkI18n instance;
  return instance;
}

void ForkI18n::syncLanguage(Language lang) {
  // No-op — language is read lazily from I18n::getInstance() in get()
  (void)lang;
}

const char* ForkI18n::get(ForkStrId id) const {
  // Lazily read current language from main I18n — always in sync, zero coupling
  int langIdx = static_cast<int>(I18n::getInstance().getLanguage());
  if (langIdx < 0 || langIdx >= FORK_LANG_COUNT) langIdx = 0;

  int strIdx = static_cast<int>(id);
  if (strIdx < 0 || strIdx >= static_cast<int>(ForkStrId::_COUNT)) return "???";

  return FORK_LANG_ARRAYS[langIdx][strIdx];
}
