#include "services/SafetyService.h"

#include <cmath>

SafetyService::SafetyService(float airMaxTemperatureC)
    : airMaxTemperatureC_(airMaxTemperatureC) {}

bool SafetyService::evaluate(const DeviceState& state, uint32_t now,
                             FaultCode& fault) const {
  (void)now;
  if (!state.heater.valid || !std::isfinite(state.heater.temperatureC)) {
    fault = FaultCode::NtcInvalid;
    return false;
  }
  if (state.heater.temperatureC >= state.setpoints.heaterLimitC &&
      state.setpoints.heaterLimitC > 0.0f) {
    fault = FaultCode::HeaterOverTemperature;
    return false;
  }
  if (!state.air.valid || !std::isfinite(state.air.temperatureC) ||
      !std::isfinite(state.air.relativeHumidity)) {
    fault = FaultCode::AirSensorInvalid;
    return false;
  }
  if (state.air.temperatureC >= airMaxTemperatureC_) {
    fault = FaultCode::AirSensorInvalid;
    return false;
  }
  if (!state.spoolOne.valid) {
    fault = FaultCode::WeightSensorOneInvalid;
    return false;
  }
  if (!state.spoolTwo.valid) {
    fault = FaultCode::WeightSensorTwoInvalid;
    return false;
  }
  fault = FaultCode::None;
  return true;
}

bool SafetyService::canHeat(const DeviceState& state) const {
  return state.fault == FaultCode::None && state.air.valid && state.heater.valid &&
         state.heater.temperatureC < state.setpoints.heaterLimitC &&
         state.air.temperatureC < airMaxTemperatureC_;
}
