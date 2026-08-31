#include "services/UiService.h"

#include <Arduino.h>

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
  const bool longPress = input_.longPress();
  const bool shortPress = input_.shortPress();
  if (delta != 0) Serial.printf("[encoder] delta=%ld\n", static_cast<long>(delta));
  if (shortPress) Serial.println("[encoder] short press");
  if (longPress) Serial.println("[encoder] long press");
  if (state.phase == DryingPhase::Idle && delta != 0) {
    selectedMode_ = static_cast<uint8_t>((selectedMode_ + (delta > 0 ? 1 : 2)) % 3);
    view_ = DisplayView::ModeMenu;
  }
  if (longPress) {
    if (state.phase != DryingPhase::Idle) {
      stateMachine_.stop(state, now);
    } else {
      view_ = DisplayView::Dashboard;
    }
  } else if (shortPress) {
    if (state.phase == DryingPhase::Paused) {
      stateMachine_.resume(state, now);
    } else if (state.phase == DryingPhase::Warmup ||
               state.phase == DryingPhase::Drying) {
      stateMachine_.pause(state, now);
    } else if (state.phase == DryingPhase::Idle) {
      if (view_ == DisplayView::Dashboard) {
        view_ = DisplayView::ModeMenu;
      } else {
        const DryingMode modes[] = {DryingMode::TimedPreset, DryingMode::TimedManual,
                                    DryingMode::Continuous};
        Setpoints setpoints;
        setpoints.airTemperatureC = 45.0f;
        setpoints.relativeHumidity = 20.0f;
        setpoints.heaterLimitC = 105.0f;
        setpoints.durationSeconds = 3600;
        if (stateMachine_.start(state, modes[selectedMode_], setpoints, now)) {
          view_ = DisplayView::Dashboard;
        }
      }
    }
  }
  if (lastRenderAt_ == 0 ||
      static_cast<uint32_t>(now - lastRenderAt_) >= defaults::kDisplayPeriodMs) {
    display_.render(state, view_, selectedMode_);
    lastRenderAt_ = now;
  }
}
