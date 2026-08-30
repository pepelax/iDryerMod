#pragma once

#include "domain/Interfaces.h"

class NtcSensor final : public IHeaterSensor {
 public:
  NtcSensor(uint8_t pin, float r25Ohms, float beta, float dividerOhms);
  bool begin() override;
  bool update(uint32_t now) override;
  HeaterReading reading() const override { return reading_; }

 private:
  float rawToCelsius(int raw) const;
  uint8_t pin_;
  float r25Ohms_;
  float beta_;
  float dividerOhms_;
  HeaterReading reading_;
};
