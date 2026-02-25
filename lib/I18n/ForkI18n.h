#pragma once

#include "ForkI18nKeys.h"
#include "I18nKeys.h"  // for Language enum

/**
 * Fork-specific i18n system — mirrors upstream I18n but for fork-only strings.
 * Keeping fork strings here means upstream I18nKeys.h is never modified,
 * making future upstream rebases conflict-free for i18n.
 */
class ForkI18n {
 public:
  static ForkI18n& getInstance();

  // Disable copy
  ForkI18n(const ForkI18n&) = delete;
  ForkI18n& operator=(const ForkI18n&) = delete;

  // Get fork-specific localized string by ID
  const char* get(ForkStrId id) const;

  // Sync language with main I18n — call whenever I18n::setLanguage() is called
  void syncLanguage(Language lang);

 private:
  ForkI18n() : _languageIndex(0) {}

  int _languageIndex;
};

// Drop-in companion to tr() for fork-specific strings
#define fork_tr(id) ForkI18n::getInstance().get(ForkStrId::id)
