#include "services/UiService.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>

#include "config/Defaults.h"

namespace {
uint8_t mainMenuRowCount() {
  size_t presetCount = 0;
  defaultPresets(presetCount);
  // presets + MANUAL + CONTINUOUS + CALIB + BACK
  return static_cast<uint8_t>(presetCount + 4);
}
}  // namespace

UiService::UiService(IDisplay& display, IInput& input,
                     DryingStateMachine& stateMachine,
                     CalibrationService& calibration, const AppConfig& config)
    : display_(display),
      input_(input),
      stateMachine_(stateMachine),
      calibration_(calibration),
      config_(config) {}

bool UiService::begin() {
  return input_.begin() && display_.begin();
}

void UiService::setToast(const char* text) {
  std::snprintf(ui_.toast, sizeof(ui_.toast), "%s", text);
  toastUntil_ = millis() + 1500;
}

void UiService::update(DeviceState& state, uint32_t now) {
  input_.update(now);
  const int32_t delta = input_.encoderDelta();
  const bool longPress = input_.longPress();
  const bool shortPress = input_.shortPress();

  // Expire the transient confirmation message.
  if (toastUntil_ != 0 && static_cast<int32_t>(now - toastUntil_) >= 0) {
    ui_.toast[0] = '\0';
    toastUntil_ = 0;
  }

  // A detent of the encoder produces several quadrature counts which arrive
  // spread over successive loop iterations. Accumulate raw counts and step
  // the UI once per detent, otherwise rows jump by two or not at all.
  if (delta != 0) {
    encoderRemainder_ += delta;
    const int32_t detents = encoderRemainder_ / defaults::kEncoderCountsPerDetent;
    encoderRemainder_ -= detents * defaults::kEncoderCountsPerDetent;
    if (detents != 0 && state.phase == DryingPhase::Idle &&
        !state.calibration.active) {
      handleRotation(detents);
    }
  }
  // A run started from the web must pull the local UI back to the dashboard.
  if (state.phase != DryingPhase::Idle && ui_.screen != UiScreen::Dashboard) {
    ui_.screen = UiScreen::Dashboard;
    ui_.editField = 0;
  }
  if (longPress) {
    handleLongPress(state, now);
  } else if (shortPress) {
    handleClick(state, now);
  }
  if (lastRenderAt_ == 0 ||
      static_cast<uint32_t>(now - lastRenderAt_) >= defaults::kDisplayPeriodMs) {
    display_.render(state, ui_);
    lastRenderAt_ = now;
  }
}

void UiService::handleRotation(int32_t detents) {
  switch (ui_.screen) {
    case UiScreen::Dashboard:
      // Any rotation on the idle screen opens the mode menu.
      enterScreen(UiScreen::MainMenu);
      break;
    case UiScreen::MainMenu:
      moveCursor(mainMenuRowCount(), detents);
      break;
    case UiScreen::ManualSetup:
      if (ui_.editField == 1) {
        ui_.manualTemperatureC =
            clampTemperature(ui_.manualTemperatureC +
                             static_cast<float>(detents));
      } else if (ui_.editField == 2) {
        ui_.manualDurationSeconds =
            adjustDuration(ui_.manualDurationSeconds, detents);
      } else {
        moveCursor(4, detents);
      }
      break;
    case UiScreen::ContinuousSetup:
      if (ui_.editField == 1) {
        ui_.continuousTemperatureC =
            clampTemperature(ui_.continuousTemperatureC +
                             static_cast<float>(detents));
      } else {
        moveCursor(3, detents);
      }
      break;
    case UiScreen::CalibrationMenu:
      moveCursor(5, detents);
      break;
    case UiScreen::ScaleSetup:
      if (ui_.editField == 1) {
        ui_.knownGrams = adjustKnownGrams(ui_.knownGrams, detents);
      } else {
        moveCursor(3, detents);
      }
      break;
    case UiScreen::DriftConfirm:
      moveCursor(2, detents);
      break;
  }
}

void UiService::handleClick(DeviceState& state, uint32_t now) {
  if (state.calibration.active) {
    // While a drift run is in progress only the long press acts (cancel).
    return;
  }
  if (state.phase == DryingPhase::Paused) {
    stateMachine_.resume(state, now);
    return;
  }
  if (state.phase == DryingPhase::Warmup || state.phase == DryingPhase::Drying) {
    stateMachine_.pause(state, now);
    return;
  }
  if (state.phase != DryingPhase::Idle) return;

  switch (ui_.screen) {
    case UiScreen::Dashboard:
      enterScreen(UiScreen::MainMenu);
      break;
    case UiScreen::MainMenu: {
      size_t presetCount = 0;
      defaultPresets(presetCount);
      if (ui_.cursor < presetCount) {
        startPreset(state, now, ui_.cursor);
      } else if (ui_.cursor == presetCount) {
        enterScreen(UiScreen::ManualSetup);
      } else if (ui_.cursor == presetCount + 1) {
        enterScreen(UiScreen::ContinuousSetup);
      } else if (ui_.cursor == presetCount + 2) {
        enterScreen(UiScreen::CalibrationMenu);
      } else {
        ui_.screen = UiScreen::Dashboard;
      }
      break;
    }
    case UiScreen::ManualSetup:
      if (ui_.editField != 0) {
        ui_.editField = 0;  // confirm the edited value
      } else if (ui_.cursor == 0) {
        ui_.editField = 1;
      } else if (ui_.cursor == 1) {
        ui_.editField = 2;
      } else if (ui_.cursor == 2) {
        startManual(state, now);
      } else {
        backToMainMenu(0);  // back to the MANUAL row
      }
      break;
    case UiScreen::ContinuousSetup:
      if (ui_.editField != 0) {
        ui_.editField = 0;
      } else if (ui_.cursor == 0) {
        ui_.editField = 1;
      } else if (ui_.cursor == 1) {
        startContinuous(state, now);
      } else {
        backToMainMenu(1);  // back to the CONTINUOUS row
      }
      break;
    case UiScreen::CalibrationMenu:
      if (ui_.cursor == 0) {
        if (calibration_.tare(state)) {
          setToast("TARED");
        } else {
          setToast("TARE ERR");
        }
      } else if (ui_.cursor == 1 || ui_.cursor == 2) {
        ui_.targetSpool = static_cast<uint8_t>(ui_.cursor - 1);
        enterScreen(UiScreen::ScaleSetup);
      } else if (ui_.cursor == 3) {
        enterScreen(UiScreen::DriftConfirm);
      } else {
        backToMainMenu(2);  // back to the CALIB row
      }
      break;
    case UiScreen::ScaleSetup:
      if (ui_.editField != 0) {
        ui_.editField = 0;
      } else if (ui_.cursor == 0) {
        ui_.editField = 1;
      } else if (ui_.cursor == 1) {
        if (calibration_.applyKnownWeight(state, ui_.targetSpool,
                                          static_cast<float>(ui_.knownGrams))) {
          setToast("SAVED");
          backToCalibrationMenu(static_cast<uint8_t>(1 + ui_.targetSpool));
        } else {
          setToast("NO MASS");
        }
      } else {
        backToCalibrationMenu(static_cast<uint8_t>(1 + ui_.targetSpool));
      }
      break;
    case UiScreen::DriftConfirm:
      if (ui_.cursor == 0) {
        if (calibration_.startDrift(state, now)) {
          ui_.screen = UiScreen::Dashboard;
        } else {
          setToast("ERR");
        }
      } else {
        backToCalibrationMenu(3);  // back to the DRIFT row
      }
      break;
  }
}

void UiService::handleLongPress(DeviceState& state, uint32_t now) {
  if (state.calibration.active) {
    // Long press cancels a drift run; any partial data is discarded.
    calibration_.cancelDrift(state);
    return;
  }
  if (state.phase != DryingPhase::Idle) {
    // Finish/Cooldown already wind down on their own; re-issuing stop would
    // only restart the cooldown timer.
    if (state.phase == DryingPhase::Finish ||
        state.phase == DryingPhase::Cooldown) {
      return;
    }
    stateMachine_.stop(state, now);
    return;
  }
  if (ui_.editField != 0) {
    ui_.editField = 0;  // cancel the edit first
    return;
  }
  // From any menu screen a long press jumps straight back to the dashboard;
  // step-by-step Back is available as a regular menu row.
  ui_.screen = UiScreen::Dashboard;
  ui_.cursor = 0;
  ui_.scrollOffset = 0;
}

void UiService::enterScreen(UiScreen screen) {
  ui_.screen = screen;
  ui_.cursor = 0;
  ui_.scrollOffset = 0;
  ui_.editField = 0;
}

void UiService::backToMainMenu(uint8_t modeOffset) {
  const uint8_t total = mainMenuRowCount();
  ui_.screen = UiScreen::MainMenu;
  ui_.editField = 0;
  ui_.cursor = 0;
  ui_.scrollOffset = 0;
  size_t presetCount = 0;
  defaultPresets(presetCount);
  const uint8_t target = static_cast<uint8_t>(presetCount + modeOffset);
  if (target < total) moveCursor(total, target);
}

void UiService::backToCalibrationMenu(uint8_t row) {
  ui_.screen = UiScreen::CalibrationMenu;
  ui_.editField = 0;
  ui_.cursor = 0;
  ui_.scrollOffset = 0;
  if (row < 5) moveCursor(5, row);
}

void UiService::moveCursor(uint8_t count, int32_t steps) {
  if (count == 0) return;
  const int32_t total = static_cast<int32_t>(count);
  int32_t cursor = static_cast<int32_t>(ui_.cursor) + steps;
  cursor = ((cursor % total) + total) % total;
  ui_.cursor = static_cast<uint8_t>(cursor);
  if (ui_.cursor < ui_.scrollOffset) {
    ui_.scrollOffset = ui_.cursor;
  } else if (ui_.cursor >=
             static_cast<uint8_t>(ui_.scrollOffset + defaults::kMenuVisibleRows)) {
    ui_.scrollOffset = static_cast<uint8_t>(
        ui_.cursor + 1 - defaults::kMenuVisibleRows);
  }
}

void UiService::startPreset(DeviceState& state, uint32_t now, uint8_t index) {
  size_t presetCount = 0;
  const DryingPreset* presets = defaultPresets(presetCount);
  if (index >= presetCount) return;
  const DryingPreset& preset = presets[index];
  Setpoints setpoints;
  setpoints.airTemperatureC = preset.airTemperatureC;
  setpoints.relativeHumidity = preset.relativeHumidity;
  setpoints.heaterLimitC = preset.heaterMaxTemperatureC;
  setpoints.durationSeconds = preset.durationSeconds;
  if (stateMachine_.start(state, DryingMode::TimedPreset, setpoints, now)) {
    std::snprintf(state.runLabel, sizeof(state.runLabel), "%s", preset.name);
    ui_.screen = UiScreen::Dashboard;
  }
}

void UiService::startManual(DeviceState& state, uint32_t now) {
  Setpoints setpoints;
  setpoints.airTemperatureC = clampTemperature(ui_.manualTemperatureC);
  setpoints.relativeHumidity = 20.0f;
  setpoints.heaterLimitC = config_.heaterMaxTemperatureC;
  setpoints.durationSeconds = ui_.manualDurationSeconds;
  if (stateMachine_.start(state, DryingMode::TimedManual, setpoints, now)) {
    std::snprintf(state.runLabel, sizeof(state.runLabel), "MANUAL");
    ui_.screen = UiScreen::Dashboard;
  }
}

void UiService::startContinuous(DeviceState& state, uint32_t now) {
  Setpoints setpoints;
  setpoints.airTemperatureC = clampTemperature(ui_.continuousTemperatureC);
  setpoints.relativeHumidity = 20.0f;
  setpoints.heaterLimitC = config_.heaterMaxTemperatureC;
  setpoints.durationSeconds = 0;
  if (stateMachine_.start(state, DryingMode::Continuous, setpoints, now)) {
    std::snprintf(state.runLabel, sizeof(state.runLabel), "CONTINUOUS");
    ui_.screen = UiScreen::Dashboard;
  }
}

float UiService::clampTemperature(float valueC) const {
  const float minimum = defaults::kManualTemperatureMinimumC;
  const float maximum =
      std::max(minimum + 1.0f, config_.airMaxTemperatureC);
  return std::min(maximum, std::max(minimum, valueC));
}

uint32_t UiService::adjustDuration(uint32_t valueSeconds, int32_t steps) const {
  const int32_t stepMinutes =
      static_cast<int32_t>(defaults::kManualDurationStepSeconds / 60UL);
  int32_t minutes = static_cast<int32_t>(valueSeconds / 60UL);
  minutes += steps * stepMinutes;
  const int32_t minimum =
      static_cast<int32_t>(defaults::kManualDurationMinSeconds / 60UL);
  const int32_t maximum =
      static_cast<int32_t>(defaults::kManualDurationMaxSeconds / 60UL);
  minutes = std::min(maximum, std::max(minimum, minutes));
  return static_cast<uint32_t>(minutes) * 60UL;
}

uint16_t UiService::adjustKnownGrams(uint16_t grams, int32_t steps) const {
  int32_t value = static_cast<int32_t>(grams) +
                  steps * static_cast<int32_t>(defaults::kKnownWeightStepG);
  value = std::min(static_cast<int32_t>(defaults::kKnownWeightMaxG),
                   std::max(static_cast<int32_t>(defaults::kKnownWeightMinG),
                            value));
  return static_cast<uint16_t>(value);
}
