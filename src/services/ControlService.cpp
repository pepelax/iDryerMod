#include "services/ControlService.h"

#include <algorithm>

#include "config/Defaults.h"
#include "control/HumidityMath.h"

ControlService::ControlService(const AppConfig& config, SafetyService& safety,
                               DryingStateMachine& stateMachine)
    : config_(config), safety_(safety), stateMachine_(stateMachine) {}

void ControlService::begin() {
  temperaturePid_.begin(config_.temperaturePid, defaults::kControlPeriodMs);
}

void ControlService::restartPulseCycle(DeviceState& state, uint32_t now) {
  PurgeParams params;
  params.minSealMs = state.setpoints.ventilation.minSealSeconds * 1000UL;
  params.settleMs = state.setpoints.ventilation.settleSeconds * 1000UL;
  params.maxPurgeMs = state.setpoints.ventilation.maxPurgeSeconds * 1000UL;
  params.riseTriggerGm3 = defaults::kPurgeRiseTriggerGm3;
  purge_.begin(params, now);
  ahSlope_.reset();
  lastAhSampleAt_ = 0;
  lastPurgePhase_ = PurgePhase::Sealed;
  lastWindowSlopeValid_ = false;
  stableWindows_ = 0;
  state.ahSlopeGm3PerHour = 0.0f;
  state.ahSlopeSamples = 0;
  state.drynessChecks = 0;
}

void ControlService::update(DeviceState& state, uint32_t now) {
  stateMachine_.update(state, now);
  // Track phase transitions up front: a run (or resume) must restart the
  // pulse-ventilation cycle, since pauses park the vent at the safe angle.
  // Hold stays active: the chamber keeps controlling at the storage
  // temperature and keeps watching for fresh moisture.
  const bool active = state.phase == DryingPhase::Precheck ||
                      state.phase == DryingPhase::Warmup ||
                      state.phase == DryingPhase::Drying ||
                      state.phase == DryingPhase::Hold;
  const bool wasActive = lastPhase_ == DryingPhase::Precheck ||
                         lastPhase_ == DryingPhase::Warmup ||
                         lastPhase_ == DryingPhase::Drying ||
                         lastPhase_ == DryingPhase::Hold;
  if (active && !wasActive) restartPulseCycle(state, now);
  lastPhase_ = state.phase;

  if (state.otaInProgress) {
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = 60;
    state.actuators.ventAngle = config_.safeVentAngle;
    return;
  }
  if (state.phase == DryingPhase::Idle || state.phase == DryingPhase::Paused ||
      state.phase == DryingPhase::Fault || state.phase == DryingPhase::Finish ||
      state.phase == DryingPhase::Cooldown) {
    state.actuators.heaterPower = 0.0f;
    state.heaterSetpointC = 0.0f;
    state.actuators.fanPower =
        (state.phase == DryingPhase::Finish || state.phase == DryingPhase::Cooldown)
            ? 60
            : 0;
    state.actuators.ventAngle = config_.safeVentAngle;
    state.ahSlopeGm3PerHour = 0.0f;
    state.ahSlopeSamples = 0;
    state.purgePhase = 0;
    return;
  }

  FaultCode fault = FaultCode::None;
  if (!safety_.evaluate(state, now, fault)) {
    stateMachine_.fault(state, fault, now);
    state.actuators.heaterPower = 0.0f;
    state.actuators.fanPower = 100;
    state.actuators.ventAngle = config_.safeVentAngle;
    return;
  }

  // Temperature cascade. Outer loop: the air-temperature PID no longer
  // outputs heater power - it outputs a heater (NTC) setpoint bounded by
  // [air target, limit - margin]. Inner loop: the thermostat holds the
  // heater inside a narrow band around that setpoint, so the heater is never
  // driven hotter than the chamber actually needs and the MOSFET switches
  // fully on/off instead of PWM-ing. In Hold the target drops to the preset
  // storage temperature.
  const float airTarget =
      state.phase == DryingPhase::Hold && state.setpoints.holdTemperatureC > 0.0f
          ? state.setpoints.holdTemperatureC
          : state.setpoints.airTemperatureC;
  const float ceiling =
      std::max(airTarget + 1.0f, heaterSetpointCeilingC(state.setpoints.heaterLimitC,
                                                         config_.heaterMaxTemperatureC));
  if (airTarget != cascadeFloor_ || ceiling != cascadeCeiling_) {
    temperaturePid_.setTunings({config_.temperaturePid.kp, config_.temperaturePid.ki,
                                config_.temperaturePid.kd, airTarget, ceiling});
    temperaturePid_.reset(airTarget);
    thermostat_.reset();
    cascadeFloor_ = airTarget;
    cascadeCeiling_ = ceiling;
  }
  state.heaterSetpointC =
      temperaturePid_.compute(state.air.temperatureC, airTarget, now);
  state.actuators.heaterPower =
      thermostat_.update(state.heater.temperatureC, state.heaterSetpointC, now)
          ? 100.0f
          : 0.0f;
  if (!safety_.canHeat(state)) state.actuators.heaterPower = 0.0f;

  // Humidity loop: pulse ventilation. The vent stays closed so moisture
  // released by the filament accumulates in the sealed chamber (the AH rise
  // is the dryness signal); short purges exchange the air once enough
  // moisture has collected.
  const VentilationPlan& plan = state.setpoints.ventilation;
  const float absoluteTarget =
      relativeToAbsoluteHumidity(airTarget, state.setpoints.relativeHumidity);
  const float purgeFloor = absoluteTarget + defaults::kPurgeFloorMarginGm3;
  const PurgePhase phase =
      purge_.update(state.air.absoluteHumidityGm3, purgeFloor, now);
  state.purgePhase = static_cast<uint8_t>(phase);

  // A sealed window that just closed is judged: a slope below the preset
  // threshold earns one dryness check; anything wetter resets the streak.
  if (lastPurgePhase_ == PurgePhase::Sealed && phase == PurgePhase::Purge) {
    const float threshold = state.setpoints.drynessSlopeGm3PerHour;
    if (threshold > 0.0f && lastWindowSlopeValid_ &&
        lastWindowSlope_ < threshold) {
      if (stableWindows_ < 255) ++stableWindows_;
    } else {
      stableWindows_ = 0;
    }
    state.drynessChecks = stableWindows_;
  }
  lastPurgePhase_ = phase;

  if (phase == PurgePhase::Sealed) {
    if (static_cast<uint32_t>(now - lastAhSampleAt_) >=
        defaults::kAhSamplePeriodMs) {
      lastAhSampleAt_ = now;
      ahSlope_.addSample(now, state.air.absoluteHumidityGm3);
      state.ahSlopeGm3PerHour = ahSlope_.slopeGm3PerHour();
      state.ahSlopeSamples = ahSlope_.sampleCount();
      lastWindowSlope_ = state.ahSlopeGm3PerHour;
      lastWindowSlopeValid_ = ahSlope_.slopeValid();
    }
  } else {
    // Purging or settling invalidates the sealed measurement window.
    ahSlope_.reset();
    state.ahSlopeGm3PerHour = 0.0f;
    state.ahSlopeSamples = 0;
  }

  const bool ventOpen = phase != PurgePhase::Sealed;
  state.actuators.ventAngle = ventOpen ? plan.ventOpenAngle : plan.ventClosedAngle;
  // Keep a circulation flow whenever the chamber is actively controlled; the
  // fan must never stop while the heater may be powered.
  const float fanDemand =
      static_cast<float>(ventOpen ? plan.purgeFanDuty : plan.circulationFanDuty);
  const float configuredMinimum =
      std::max(0.0f, std::min(100.0f, config_.fanMinimumDuty));
  const float boundedFan =
      std::max(defaults::kActiveFanMinimumDuty,
               std::max(configuredMinimum, std::max(0.0f, std::min(100.0f, fanDemand))));
  state.actuators.fanPower = static_cast<uint8_t>(boundedFan);
}
