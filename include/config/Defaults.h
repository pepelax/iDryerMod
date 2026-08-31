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
// Temperature cascade: the air PID outputs a heater (NTC) setpoint bounded
// by [air target, heater limit - margin]; the inner thermostat then holds the
// heater inside a +/-hysteresis/2 band around it.
constexpr float kHeaterThermostatHysteresisC = 2.0f;
constexpr float kHeaterSetpointMarginC = 5.0f;
constexpr uint32_t kHeaterMinSwitchIntervalMs = 2000;
constexpr uint32_t kAirSensorPeriodMs = 1000;
constexpr uint32_t kNtcPeriodMs = 100;
constexpr uint32_t kWeightPeriodMs = 100;
constexpr uint32_t kHumidityPidPeriodMs = 5000;
constexpr uint32_t kDisplayPeriodMs = 250;
// Half-quadrature decoding reports two counts per mechanical detent.
constexpr uint8_t kEncoderCountsPerDetent = 2;
constexpr uint8_t kMenuVisibleRows = 4;
constexpr float kManualTemperatureMinimumC = 20.0f;
constexpr uint32_t kManualDurationStepSeconds = 1800;   // 30 minutes
constexpr uint32_t kManualDurationMinSeconds = 1800;    // 30 minutes
constexpr uint32_t kManualDurationMaxSeconds = 43200;   // 12 hours
constexpr uint32_t kHistoryPeriodMs = 60000;
// Pulse ventilation and dryness estimation.
constexpr float kPurgeRiseTriggerGm3 = 0.5f;   // AH rise that justifies a purge
constexpr float kPurgeFloorMarginGm3 = 3.0f;   // saturation guard above target
constexpr uint32_t kAhSamplePeriodMs = 30000;  // sealed-window sampling rate
constexpr uint8_t kAhSlopeMinSamples = 8;      // ~4 min before slope is valid
// Consecutive quiet sealed windows required before a preset run may move
// from Drying to Hold (empirical; recalibrate from logged slope data).
constexpr uint8_t kDrynessStableWindows = 2;
// Drift calibration: the chamber is heated and cooled with a reference mass
// on the scales, sampling (temperature, raw) pairs into 5C bands.
constexpr uint8_t kWeightCalBandWidthC = 5;
constexpr float kWeightCalBaseTempC = 20.0f;
constexpr float kWeightCalTargetC = 80.0f;
constexpr uint32_t kWeightCalPointPeriodMs = 30000;
constexpr uint32_t kWeightCalMaxDurationMs = 6UL * 3600UL * 1000UL;
constexpr uint16_t kKnownWeightStepG = 25;
constexpr uint16_t kKnownWeightMinG = 25;
constexpr uint16_t kKnownWeightMaxG = 5000;
constexpr uint32_t kWebPollPeriodMs = 1000;
constexpr uint32_t kDiagnosticsPeriodMs = 5000;
constexpr uint32_t kWatchdogTimeoutSeconds = 5;
constexpr uint8_t kWeightFilterSize = 8;

}  // namespace defaults
