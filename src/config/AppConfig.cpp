#include "config/AppConfig.h"

#include <cstring>

#include "config/Defaults.h"

void setDefaultConfig(AppConfig& config) {
  std::memset(&config, 0, sizeof(config));
  std::strncpy(config.hostname, "dryer", sizeof(config.hostname) - 1);
  config.ntcR25Ohms = defaults::kNtcR25Ohms;
  config.ntcBeta = defaults::kNtcBeta;
  config.ntcDividerOhms = defaults::kNtcDividerOhms;
  config.heaterMaxTemperatureC = defaults::kHeaterMaxTemperatureC;
  config.airMaxTemperatureC = defaults::kAirMaxTemperatureC;
  config.fanMinimumDuty = 25.0f;
  config.fanStartupDuty = 100.0f;
  config.servoMinUs = defaults::kServoMinUs;
  config.servoMaxUs = defaults::kServoMaxUs;
  config.safeVentAngle = defaults::kSafeVentAngle;
  config.temperaturePid = {8.0f, 0.08f, 1.0f, 0.0f, 100.0f};
  config.humidityPid = {2.0f, 0.02f, 0.0f, 0.0f, 100.0f};
  config.weightScaleOne = 1.0f;
  config.weightScaleTwo = 1.0f;
}

const DryingPreset* defaultPresets(size_t& count) {
  static const DryingPreset presets[] = {
      {"pla", "PLA", 45.0f, 40.0f, 20.0f, 0.3f, 4UL * 3600UL, 8UL * 3600UL,
       95.0f, {25, 100, 15, 90, 900, 30}},
      {"petg", "PETG", 55.0f, 45.0f, 20.0f, 0.3f, 5UL * 3600UL, 10UL * 3600UL,
       100.0f, {25, 100, 15, 90, 600, 30}},
      {"abs", "ABS/ASA", 65.0f, 50.0f, 15.0f, 0.3f, 4UL * 3600UL, 8UL * 3600UL,
       105.0f, {30, 100, 15, 90, 600, 45}},
      {"tpu", "TPU", 50.0f, 45.0f, 15.0f, 0.3f, 6UL * 3600UL, 12UL * 3600UL,
       100.0f, {25, 100, 15, 90, 900, 30}},
      {"pa", "PA/Nylon", 70.0f, 55.0f, 10.0f, 0.3f, 8UL * 3600UL, 16UL * 3600UL,
       105.0f, {35, 100, 15, 90, 450, 60}},
      {"pc", "PC (experimental)", 75.0f, 55.0f, 10.0f, 0.3f, 6UL * 3600UL,
       12UL * 3600UL, 105.0f, {40, 100, 15, 90, 300, 60}},
  };
  count = sizeof(presets) / sizeof(presets[0]);
  return presets;
}
