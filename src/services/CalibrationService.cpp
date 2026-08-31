#include "services/CalibrationService.h"

#include <Arduino.h>

#include <cmath>

#include "config/Defaults.h"

CalibrationService::CalibrationService(AppConfig& config,
                                       DryingStateMachine& stateMachine,
                                       IStorage& storage,
                                       IWeightSensor& spoolOne,
                                       IWeightSensor& spoolTwo)
    : config_(config),
      stateMachine_(stateMachine),
      storage_(storage),
      spoolOne_(spoolOne),
      spoolTwo_(spoolTwo) {}

void CalibrationService::begin() {
  storage_.loadWeightCalibration(0, tables_[0]);
  storage_.loadWeightCalibration(1, tables_[1]);
}

IWeightSensor& CalibrationService::sensorOf(uint8_t spool) const {
  return spool == 1 ? spoolTwo_ : spoolOne_;
}

uint8_t CalibrationService::bandOf(float temperatureC) {
  const int index = static_cast<int>(
      (temperatureC - defaults::kWeightCalBaseTempC) /
      defaults::kWeightCalBandWidthC);
  if (index < 0) return 0;
  if (index >= kWeightCalBands) return kWeightCalBands - 1;
  return static_cast<uint8_t>(index);
}

float CalibrationService::coefficientOf(uint8_t spool,
                                        float temperatureC) const {
  const WeightCalTable& table = tables_[spool];
  if (!table.valid) return 1.0f;
  return table.coeff[bandOf(temperatureC)];
}

const WeightCalTable& CalibrationService::table(uint8_t spool) const {
  return tables_[spool > 0 ? 1 : 0];
}

void CalibrationService::update(DeviceState& state, uint32_t now) {
  // Temperature compensation of the displayed weights. The driver already
  // computed grams at the reference temperature, so the correction is a
  // plain multiplier.
  if (state.air.valid && state.spoolOne.valid) {
    state.spoolOne.grams *= coefficientOf(0, state.air.temperatureC);
  }
  if (state.air.valid && state.spoolTwo.valid) {
    state.spoolTwo.grams *= coefficientOf(1, state.air.temperatureC);
  }

  if (!state.calibration.active) return;

  // A safety fault aborts the session without saving anything.
  if (state.phase == DryingPhase::Fault || state.fault != FaultCode::None) {
    cancelDrift(state);
    return;
  }
  // Hard session timeout: never leave the chamber hot unattended.
  if (static_cast<uint32_t>(now - sessionStartedAt_) >=
      defaults::kWeightCalMaxDurationMs) {
    finishDrift(state, false);
    return;
  }

  // Sample (temperature, raw) pairs into their bands periodically.
  if (lastSampleAt_ == 0 ||
      static_cast<uint32_t>(now - lastSampleAt_) >=
          defaults::kWeightCalPointPeriodMs) {
    lastSampleAt_ = now;
    if (state.air.valid) {
      const uint8_t band = bandOf(state.air.temperatureC);
      bool sampled = false;
      if (state.spoolOne.valid) {
        bandSums_[0][band] += static_cast<float>(state.spoolOne.raw);
        ++bandCounts_[0][band];
        sampled = true;
      }
      if (state.spoolTwo.valid) {
        bandSums_[1][band] += static_cast<float>(state.spoolTwo.raw);
        ++bandCounts_[1][band];
        sampled = true;
      }
      if (sampled) ++state.calibration.points;
    }
  }

  if (state.calibration.phase == 1) {
    // Heat until the target is reached, then hand over to the cool-down.
    if (state.air.valid &&
        state.air.temperatureC >= state.calibration.targetTempC - 1.0f) {
      stateMachine_.stop(state, now);
      state.calibration.phase = 2;
      lastSampleAt_ = 0;
    }
  } else if (state.calibration.phase == 2) {
    // Keep the fans running while cooling so the cycle finishes sooner.
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = 100;
    state.actuators.ventAngle = config_.safeVentAngle;
    // Back near the starting temperature: the sweep is complete.
    if (state.air.valid &&
        state.air.temperatureC <= state.calibration.startTempC + 1.5f) {
      finishDrift(state, true);
    }
  }
}

bool CalibrationService::tare(DeviceState& state) {
  (void)state;
  spoolOne_.tare();
  spoolTwo_.tare();
  config_.weightTareOne = spoolOne_.tareRaw();
  config_.weightTareTwo = spoolTwo_.tareRaw();
  storage_.saveConfig(config_);
  return true;
}

bool CalibrationService::applyKnownWeight(DeviceState& state, uint8_t spool,
                                          float knownGrams) {
  if (spool > 1) return false;
  if (knownGrams < 1.0f || !std::isfinite(knownGrams)) return false;
  const WeightReading& reading = spool == 0 ? state.spoolOne : state.spoolTwo;
  if (!reading.valid) return false;
  const int32_t tareRaw = spool == 0 ? spoolOne_.tareRaw() : spoolTwo_.tareRaw();
  const float span = static_cast<float>(reading.raw - tareRaw);
  // A vanishing span means the mass is not on the scale yet.
  if (std::fabs(span) < 100.0f) return false;
  const float scale = span / knownGrams;
  if (!std::isfinite(scale) || std::fabs(scale) < 1e-6f) return false;
  sensorOf(spool).setScale(scale);
  if (spool == 0) {
    config_.weightScaleOne = scale;
  } else {
    config_.weightScaleTwo = scale;
  }
  storage_.saveConfig(config_);
  return true;
}

void CalibrationService::resetAccumulators() {
  for (uint8_t sensor = 0; sensor < 2; ++sensor) {
    for (uint8_t band = 0; band < kWeightCalBands; ++band) {
      bandCounts_[sensor][band] = 0;
      bandSums_[sensor][band] = 0.0f;
    }
  }
  lastSampleAt_ = 0;
}

bool CalibrationService::startDrift(DeviceState& state, uint32_t now) {
  if (state.calibration.active) return false;
  if (!state.spoolOne.valid && !state.spoolTwo.valid) return false;
  if (!state.air.valid) return false;
  // The safety layer cuts heating at airMaxTemperatureC, so aim slightly
  // below it (and below the nominal 80 C).
  const float target = std::fmin(
      defaults::kWeightCalTargetC, config_.airMaxTemperatureC - 2.0f);
  Setpoints setpoints;
  setpoints.airTemperatureC = target;
  setpoints.relativeHumidity = 20.0f;
  setpoints.heaterLimitC = config_.heaterMaxTemperatureC;
  setpoints.durationSeconds = 0;
  if (!stateMachine_.start(state, DryingMode::Calibration, setpoints, now)) {
    return false;
  }
  resetAccumulators();
  sessionStartedAt_ = now;
  referenceBand_ = bandOf(state.air.temperatureC);
  state.calibration.active = true;
  state.calibration.phase = 1;
  state.calibration.points = 0;
  state.calibration.targetTempC = target;
  state.calibration.startTempC = state.air.temperatureC;
  return true;
}

void CalibrationService::cancelDrift(DeviceState& state) {
  finishDrift(state, false);
}

void CalibrationService::syncSensors() {
  spoolOne_.setScale(config_.weightScaleOne);
  spoolTwo_.setScale(config_.weightScaleTwo);
  spoolOne_.setTareRaw(config_.weightTareOne);
  spoolTwo_.setTareRaw(config_.weightTareTwo);
}

void CalibrationService::finishDrift(DeviceState& state, bool success) {
  if (state.calibration.phase == 1 && state.phase != DryingPhase::Idle) {
    stateMachine_.stop(state, millis());
  }
  if (success) {
    // Build a coefficient table per sensor: every band's mean raw value is
    // normalised against the reference (room temperature) band.
    for (uint8_t sensor = 0; sensor < 2; ++sensor) {
      const uint32_t refCount = bandCounts_[sensor][referenceBand_];
      if (refCount == 0) continue;
      const float refMean = bandSums_[sensor][referenceBand_] / refCount;
      if (refMean == 0.0f) continue;
      WeightCalTable table;
      uint8_t filled = 0;
      for (uint8_t band = 0; band < kWeightCalBands; ++band) {
        if (bandCounts_[sensor][band] == 0) continue;
        const float mean =
            bandSums_[sensor][band] / bandCounts_[sensor][band];
        // Guard against a division blow-up on a near-zero mean.
        table.coeff[band] =
            std::fabs(mean) > 1.0f ? refMean / mean : 1.0f;
        ++filled;
      }
      if (filled >= 3) {
        table.valid = true;
        table.filledBands = filled;
        tables_[sensor] = table;
        storage_.saveWeightCalibration(sensor, table);
      }
    }
  }
  state.calibration.active = false;
  state.calibration.phase = 0;
  state.calibration.points = 0;
}
