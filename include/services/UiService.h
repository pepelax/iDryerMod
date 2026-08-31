#pragma once

#include <stdint.h>

#include "config/AppConfig.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"
#include "services/CalibrationService.h"

class UiService {
 public:
  UiService(IDisplay& display, IInput& input, DryingStateMachine& stateMachine,
            CalibrationService& calibration, const AppConfig& config);
  bool begin();
  void update(DeviceState& state, uint32_t now);

 private:
  void handleRotation(int32_t detents);
  void handleClick(DeviceState& state, uint32_t now);
  void handleLongPress(DeviceState& state, uint32_t now);
  void enterScreen(UiScreen screen);
  // Returns to the main menu with the cursor on a mode row (0 = MANUAL,
  // 1 = CONTINUOUS, 2 = CALIB) instead of the top of the list.
  void backToMainMenu(uint8_t modeOffset);
  // Returns to the calibration menu with the cursor on the given row.
  void backToCalibrationMenu(uint8_t row);
  void moveCursor(uint8_t count, int32_t steps);
  void startManual(DeviceState& state, uint32_t now);
  void startContinuous(DeviceState& state, uint32_t now);
  void startPreset(DeviceState& state, uint32_t now, uint8_t index);
  float clampTemperature(float valueC) const;
  uint32_t adjustDuration(uint32_t valueSeconds, int32_t steps) const;
  uint16_t adjustKnownGrams(uint16_t grams, int32_t steps) const;
  void setToast(const char* text);

  IDisplay& display_;
  IInput& input_;
  DryingStateMachine& stateMachine_;
  CalibrationService& calibration_;
  const AppConfig& config_;
  UiState ui_;
  int32_t encoderRemainder_ = 0;
  uint32_t lastRenderAt_ = 0;
  uint32_t toastUntil_ = 0;
};
