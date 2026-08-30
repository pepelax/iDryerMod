#include "services/ControlService.h"

#include <algorithm>

#include "config/Defaults.h"
#include "control/HumidityMath.h"

ControlService::ControlService(const AppConfig& config, SafetyService& safety,
                               DryingStateMachine& stateMachine)
    : config_(config), safety_(safety), stateMachine_(stateMachine) {}

void ControlService::begin() {
  temperaturePid_.begin(config_.temperaturePid, defaults::kControlPeriodMs);
  humidityPid_.begin(config_.humidityPid, defaults::kHumidityPidPeriodMs);
}

void ControlService::update(DeviceState& state, uint32_t now) {
  stateMachine_.update(state, now);
  if (state.otaInProgress) {
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = 60;
    state.actuators.ventAngle = config_.safeVentAngle;
    return;
  }
  if (state.phase == DryingPhase::Idle || state.phase == DryingPhase::Paused ||
      state.phase == DryingPhase::Fault || state.phase == DryingPhase::Cooldown) {
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = state.phase == DryingPhase::Cooldown ? 60 : 0;
    return;
  }

  FaultCode fault = FaultCode::None;
  if (!safety_.evaluate(state, now, fault)) {
    stateMachine_.fault(state, fault, now);
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = 100;
    return;
  }

  state.actuators.heaterPower =
      temperaturePid_.compute(state.air.temperatureC, state.setpoints.airTemperatureC, now);
  if (!safety_.canHeat(state)) state.actuators.heaterPower = 0.0f;

  const float absoluteTarget =
      relativeToAbsoluteHumidity(state.setpoints.airTemperatureC,
                                 state.setpoints.relativeHumidity);
  // Reverse-acting loop: demand must grow when measured moisture is above target.
  const float humidityDemand =
      humidityPid_.compute(absoluteTarget, state.air.absoluteHumidityGm3, now);
  const float boundedDemand = std::max(0.0f, std::min(100.0f, humidityDemand));
  state.actuators.fanPower = static_cast<uint8_t>(boundedDemand);
  state.actuators.ventAngle = static_cast<uint16_t>(
      15.0f + (90.0f - 15.0f) * boundedDemand / 100.0f);
}
