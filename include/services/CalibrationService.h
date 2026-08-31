#pragma once

#include <stdint.h>

#include "config/AppConfig.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"

// Weight-sensor calibration: tare, scale with a known reference mass and a
// long heat/cool drift run that builds a per-temperature-band correction
// table for each load cell.
class CalibrationService {
 public:
  CalibrationService(AppConfig& config, DryingStateMachine& stateMachine,
                     IStorage& storage, IWeightSensor& spoolOne,
                     IWeightSensor& spoolTwo);

  void begin();
  // Must run after ControlService::update and before actuators are applied:
  // it compensates the displayed weights and, during the cool-down phase of
  // a drift run, keeps the fans spinning to speed up cooling.
  void update(DeviceState& state, uint32_t now);

  bool tare(DeviceState& state);
  bool applyKnownWeight(DeviceState& state, uint8_t spool, float knownGrams);
  bool startDrift(DeviceState& state, uint32_t now);
  void cancelDrift(DeviceState& state);
  // Pushes scale/tare values from the config into the live sensors (used
  // after the web config endpoint changed them).
  void syncSensors();
  const WeightCalTable& table(uint8_t spool) const;

 private:
  static uint8_t bandOf(float temperatureC);
  float coefficientOf(uint8_t spool, float temperatureC) const;
  IWeightSensor& sensorOf(uint8_t spool) const;
  void resetAccumulators();
  void finishDrift(DeviceState& state, bool success);

  AppConfig& config_;
  DryingStateMachine& stateMachine_;
  IStorage& storage_;
  IWeightSensor& spoolOne_;
  IWeightSensor& spoolTwo_;
  WeightCalTable tables_[2];
  // Drift-session accumulators: raw sums per temperature band and sensor.
  uint32_t bandCounts_[2][kWeightCalBands] = {};
  float bandSums_[2][kWeightCalBands] = {};
  uint32_t lastSampleAt_ = 0;
  uint32_t sessionStartedAt_ = 0;
  uint8_t referenceBand_ = 0;
};
