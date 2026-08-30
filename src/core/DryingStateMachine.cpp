#include "core/DryingStateMachine.h"

void DryingStateMachine::begin(DeviceState& state, uint32_t now) {
  state.mode = DryingMode::Idle;
  state.phase = DryingPhase::Idle;
  state.fault = FaultCode::None;
  state.phaseStartedAt = now;
  state.runStartedAt = 0;
  state.remainingSeconds = 0;
  durationSeconds_ = 0;
  pausedAt_ = 0;
  pausedFrom_ = DryingPhase::Idle;
}

bool DryingStateMachine::start(DeviceState& state, DryingMode mode,
                               const Setpoints& setpoints, uint32_t now) {
  if (mode != DryingMode::TimedPreset && mode != DryingMode::TimedManual &&
      mode != DryingMode::Continuous) {
    return false;
  }
  durationSeconds_ = setpoints.durationSeconds;
  state.mode = mode;
  state.phase = DryingPhase::Precheck;
  state.setpoints = setpoints;
  state.fault = FaultCode::None;
  state.phaseStartedAt = now;
  state.runStartedAt = 0;
  state.remainingSeconds = durationSeconds_;
  return true;
}

void DryingStateMachine::pause(DeviceState& state, uint32_t now) {
  if (state.phase == DryingPhase::Warmup || state.phase == DryingPhase::Drying) {
    pausedFrom_ = state.phase;
    state.phase = DryingPhase::Paused;
    pausedAt_ = now;
  }
}

void DryingStateMachine::resume(DeviceState& state, uint32_t now) {
  if (state.phase == DryingPhase::Paused) {
    if (state.runStartedAt != 0 && pausedAt_ != 0) {
      state.runStartedAt += static_cast<uint32_t>(now - pausedAt_);
    }
    state.phase = pausedFrom_;
    state.phaseStartedAt = now;
    pausedAt_ = 0;
    pausedFrom_ = DryingPhase::Idle;
  }
}

void DryingStateMachine::stop(DeviceState& state, uint32_t now) {
  state.mode = DryingMode::Idle;
  state.phase = DryingPhase::Cooldown;
  state.phaseStartedAt = now;
  state.remainingSeconds = 0;
}

void DryingStateMachine::fault(DeviceState& state, FaultCode code, uint32_t now) {
  state.mode = DryingMode::Fault;
  state.phase = DryingPhase::Fault;
  state.fault = code;
  state.phaseStartedAt = now;
}

void DryingStateMachine::update(DeviceState& state, uint32_t now) {
  if (state.phase == DryingPhase::Precheck) {
    state.phase = DryingPhase::Warmup;
    state.phaseStartedAt = now;
  } else if (state.phase == DryingPhase::Warmup) {
    if (state.air.valid &&
        state.air.temperatureC >= state.setpoints.airTemperatureC - 1.0f) {
      state.phase = DryingPhase::Drying;
      state.phaseStartedAt = now;
      state.runStartedAt = now;
    }
  } else if (state.phase == DryingPhase::Drying &&
             state.mode != DryingMode::Continuous) {
    const uint32_t elapsed = (now - state.runStartedAt) / 1000UL;
    state.remainingSeconds = elapsed >= durationSeconds_ ? 0 : durationSeconds_ - elapsed;
    if (state.remainingSeconds == 0) {
      state.phase = DryingPhase::Finish;
      state.phaseStartedAt = now;
    }
  } else if (state.phase == DryingPhase::Finish &&
             static_cast<uint32_t>(now - state.phaseStartedAt) >= 30000UL) {
    state.mode = DryingMode::Idle;
    state.phase = DryingPhase::Idle;
  } else if (state.phase == DryingPhase::Cooldown &&
             static_cast<uint32_t>(now - state.phaseStartedAt) >= 30000UL) {
    state.mode = DryingMode::Idle;
    state.phase = DryingPhase::Idle;
  }
}
