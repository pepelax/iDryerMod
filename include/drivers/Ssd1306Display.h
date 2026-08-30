#pragma once

#include <U8g2lib.h>

#include "domain/Interfaces.h"

class Ssd1306Display final : public IDisplay {
 public:
  explicit Ssd1306Display(uint8_t address);
  bool begin() override;
  void render(const DeviceState& state) override;

 private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C display_;
  uint8_t address_;
};
