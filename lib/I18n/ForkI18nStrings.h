#pragma once

#include "ForkI18nKeys.h"

// Fork-specific string arrays — one per language, indexed by ForkStrId.
// Array order matches Language enum in I18nKeys.h:
//   0=EN, 1=ES, 2=FR, 3=DE, 4=CZ, 5=PO, 6=RU, 7=SV, 8=RO, 9=CA, 10=UK, 11=BE

namespace fork_i18n_strings {

extern const char* const FORK_STRINGS_EN[];
extern const char* const FORK_STRINGS_ES[];
extern const char* const FORK_STRINGS_FR[];
extern const char* const FORK_STRINGS_DE[];
extern const char* const FORK_STRINGS_CZ[];
extern const char* const FORK_STRINGS_PO[];
extern const char* const FORK_STRINGS_RU[];
extern const char* const FORK_STRINGS_SV[];
extern const char* const FORK_STRINGS_RO[];
extern const char* const FORK_STRINGS_CA[];
extern const char* const FORK_STRINGS_UK[];
extern const char* const FORK_STRINGS_BE[];

}  // namespace fork_i18n_strings
