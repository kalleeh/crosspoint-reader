#pragma once

#include <functional>
#include <string>

#include "../Activity.h"

class WeatherActivity final : public Activity {
 private:
  enum State { LOADING, LOADED, ERROR };

  State state;
  const std::function<void()> onBack;
  
  // Weather data
  std::string location;
  int temperature;
  int feelsLike;
  std::string condition;
  int humidity;
  int windSpeed;
  
  unsigned long lastUpdate;

  void fetchWeather();
  void render();
  void drawWeatherIcon(int x, int y, int size, const char* condition);
  bool loadWeatherBackground(const char* condition);

 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const std::function<void()>& onBack)
      : Activity("Weather", renderer, mappedInput), onBack(onBack), state(LOADING),
        temperature(0), feelsLike(0), humidity(0), windSpeed(0), lastUpdate(0) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return state == LOADING; }
};
