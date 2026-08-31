#pragma once

#include <U8g2lib.h>

#include "domain/Interfaces.h"

class Sh1106Display final : public IDisplay {
 public:
  explicit Sh1106Display(uint8_t address);
  bool begin() override;
  void render(const DeviceState& state) override;
  void render(const DeviceState& state, DisplayView view,
              uint8_t selectedMode) override;

 private:
  void renderIdle(const DeviceState& state);
  void renderModeMenu(uint8_t selectedMode);
  void renderActive(const DeviceState& state);
  void renderFault(const DeviceState& state);

  U8G2_SH1106_128X64_NONAME_F_HW_I2C display_;
  uint8_t address_;
};
