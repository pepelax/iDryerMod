#pragma once

#include <U8g2lib.h>

#include "domain/Interfaces.h"

class Sh1106Display final : public IDisplay {
 public:
  explicit Sh1106Display(uint8_t address);
  bool begin() override;
  void render(const DeviceState& state) override;
  void render(const DeviceState& state, const UiState& ui) override;

 private:
  void renderIdle(const DeviceState& state);
  void renderMainMenu(const UiState& ui);
  void renderManualSetup(const UiState& ui);
  void renderContinuousSetup(const UiState& ui);
  void renderCalibrationMenu(const UiState& ui);
  void renderScaleSetup(const DeviceState& state, const UiState& ui);
  void renderDriftConfirm(const UiState& ui);
  void renderCalProgress(const DeviceState& state);
  void renderActive(const DeviceState& state);
  void renderFault(const DeviceState& state);
  void drawMenu(const char* title, const char* const* lines, uint8_t count,
                const UiState& ui, uint8_t editRow,
                const char* rightTitle = nullptr);

  U8G2_SH1106_128X64_NONAME_F_HW_I2C display_;
  uint8_t address_;
};
