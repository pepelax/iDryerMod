#pragma once

#include "config/AppConfig.h"

namespace defaults {

constexpr float kNtcR25Ohms = 10000.0f;
constexpr float kNtcBeta = 3950.0f;
constexpr float kNtcDividerOhms = 10000.0f;
constexpr float kHeaterMaxTemperatureC = 105.0f;
constexpr float kAirMaxTemperatureC = 75.0f;
constexpr uint16_t kServoMinUs = 500;
constexpr uint16_t kServoMaxUs = 2500;
constexpr uint8_t kSafeVentAngle = 90;
constexpr uint32_t kHeaterWindowMs = 1000;
constexpr uint32_t kControlPeriodMs = 100;
constexpr uint32_t kAirSensorPeriodMs = 1000;
constexpr uint32_t kNtcPeriodMs = 100;
constexpr uint32_t kWeightPeriodMs = 100;
constexpr uint32_t kHumidityPidPeriodMs = 5000;
constexpr uint32_t kDisplayPeriodMs = 250;
constexpr uint32_t kHistoryPeriodMs = 60000;
constexpr uint32_t kWebPollPeriodMs = 1000;
constexpr uint32_t kWatchdogTimeoutSeconds = 5;
constexpr uint8_t kWeightFilterSize = 8;

}  // namespace defaults
