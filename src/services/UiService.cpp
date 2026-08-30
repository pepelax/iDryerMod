#include "services/UiService.h"

#include "config/Defaults.h"

UiService::UiService(IDisplay& display, IInput& input,
                     DryingStateMachine& stateMachine)
    : display_(display), input_(input), stateMachine_(stateMachine) {}

bool UiService::begin() {
  return input_.begin() && display_.begin();
}

void UiService::update(DeviceState& state, uint32_t now) {
  input_.update(now);
  const int32_t delta = input_.encoderDelta();
  if (state.phase == DryingPhase::Idle && delta != 0) {
    selectedMode_ = static_cast<uint8_t>((selectedMode_ + (delta > 0 ? 1 : 2)) % 3);
  }
  if (input_.longPress()) {
    stateMachine_.stop(state, now);
  } else if (input_.shortPress()) {
    if (state.phase == DryingPhase::Paused) {
      stateMachine_.resume(state, now);
    } else if (state.phase == DryingPhase::Warmup ||
               state.phase == DryingPhase::Drying) {
      stateMachine_.pause(state, now);
    } else if (state.phase == DryingPhase::Idle) {
      const DryingMode modes[] = {DryingMode::TimedPreset, DryingMode::TimedManual,
                                  DryingMode::Continuous};
      Setpoints setpoints;
      setpoints.airTemperatureC = 45.0f;
      setpoints.relativeHumidity = 20.0f;
      setpoints.heaterLimitC = 105.0f;
      setpoints.durationSeconds = 3600;
      stateMachine_.start(state, modes[selectedMode_], setpoints, now);
    }
  }
  if (lastRenderAt_ == 0 ||
      static_cast<uint32_t>(now - lastRenderAt_) >= defaults::kDisplayPeriodMs) {
    display_.render(state);
    lastRenderAt_ = now;
  }
}
