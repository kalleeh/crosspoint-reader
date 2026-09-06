#pragma once

#include <string>

#include "../Activity.h"

class WeatherActivity final : public Activity {
 private:
  enum State { LOADING, LOADED, ERROR };

  State state;
  void (*const onBack)();

  // Weather data
  std::string location;
  int temperature;
  int feelsLike;
  std::string condition;
  int humidity;
  int windSpeed;

  unsigned long lastUpdate;

  void fetchWeather(bool allowCache = false);
  void render();
  void drawWeatherIcon(int x, int y, int size, const char* condition);
  bool loadWeatherBackground(const char* condition);

 public:
  explicit WeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void (*onBack)())
      : Activity("Weather", renderer, mappedInput),
        onBack(onBack),
        state(LOADING),
        location(),
        condition(),
        temperature(0),
        feelsLike(0),
        humidity(0),
        windSpeed(0),
        lastUpdate(0) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return state == LOADING; }
};
