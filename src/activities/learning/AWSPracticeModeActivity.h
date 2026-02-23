#pragma once

#include "../Activity.h"
#include "../../MappedInputManager.h"
#include <GfxRenderer.h>
#include <functional>
#include <vector>

class AWSPracticeModeActivity final : public Activity {
  const std::function<void()> onBack;
  const std::function<void(const char*, const char*)> onSelectMode;  // mode, domain
  String certId;
  
  enum MenuState { MODE_SELECT, DOMAIN_SELECT };
  MenuState menuState = MODE_SELECT;
  
  struct ModeOption {
    const char* id;
    const char* name;
    const char* description;
  };
  
  std::vector<ModeOption> modes;
  std::vector<String> domains;
  int selectedIndex = 0;
  int scrollOffset = 0;
  
  void render();
  void renderDomainSelect();
  void loadDomains();
  
 public:
  explicit AWSPracticeModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onBack,
                                   const std::function<void(const char*, const char*)>& onSelectMode,
                                   const char* certId)
      : Activity("Practice Mode", renderer, mappedInput), 
        onBack(onBack), onSelectMode(onSelectMode), certId(certId) {
    
    modes.push_back({"full", "Full Exam", "Complete practice exam (65-75 questions)"});
    modes.push_back({"quick", "Quick Practice", "15 random questions"});
    modes.push_back({"quickstart", "Quick Start 🔥", "Just 5 questions - easy start!"});
    modes.push_back({"study", "Study Mode", "20 questions with instant feedback"});
    modes.push_back({"domain", "Practice by Domain", "Focus on specific exam domain"});
    modes.push_back({"review", "Review Incorrect", "Practice questions you got wrong"});
  }
  
  void onEnter() override;
  void loop() override;
};
