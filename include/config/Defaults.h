#pragma once

#include "config/AppConfig.h"

namespace defaults {

constexpr float kNtcR25Ohms = 10000.0f;
constexpr float kNtcBeta = 3950.0f;
// Fixed resistor in the NTC divider used on the test/control board.
constexpr float kNtcDividerOhms = 3600.0f;
constexpr float kHeaterMaxTemperatureC = 105.0f;
constexpr float kAirMaxTemperatureC = 75.0f;
constexpr float kActiveFanMinimumDuty = 25.0f;
constexpr uint16_t kServoMinUs = 500;
constexpr uint16_t kServoMaxUs = 2500;
constexpr uint8_t kSafeVentAngle = 90;
constexpr uint8_t kServoClosedAngle = 15;
constexpr uint8_t kServoOpenAngle = 90;
constexpr uint8_t kServoMovementThresholdDegrees = 2;
constexpr uint32_t kServoReleaseDelayMs = 500;
constexpr uint32_t kHeaterWindowMs = 1000;
constexpr uint32_t kControlPeriodMs = 100;
constexpr uint32_t kAirSensorPeriodMs = 1000;
constexpr uint32_t kNtcPeriodMs = 100;
constexpr uint32_t kWeightPeriodMs = 100;
constexpr uint32_t kHumidityPidPeriodMs = 5000;
constexpr uint32_t kDisplayPeriodMs = 250;
constexpr uint32_t kHistoryPeriodMs = 60000;
constexpr uint32_t kWebPollPeriodMs = 1000;
constexpr uint32_t kDiagnosticsPeriodMs = 5000;
constexpr uint32_t kWatchdogTimeoutSeconds = 5;
constexpr uint8_t kWeightFilterSize = 8;

}  // namespace defaults
