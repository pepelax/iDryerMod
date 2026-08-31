#pragma once

#include <HX711.h>

#include "domain/Interfaces.h"

class Hx711Weight final : public IWeightSensor {
 public:
  Hx711Weight(uint8_t doutPin, uint8_t sckPin);
  bool begin() override;
  bool update(uint32_t now) override;
  WeightReading reading() const override { return reading_; }
  void tare() override;
  void setScale(float scale) override { scale_ = scale == 0.0f ? 1.0f : scale; }
  void setTareRaw(int32_t tareRaw) override { tareRaw_ = tareRaw; }
  int32_t tareRaw() const override { return tareRaw_; }

 private:
  HX711 adc_;
  uint8_t doutPin_;
  uint8_t sckPin_;
  float scale_ = 1.0f;
  int32_t tareRaw_ = 0;
  WeightReading reading_;
  int32_t samples_[8] = {};
  uint8_t sampleCount_ = 0;
  uint8_t sampleIndex_ = 0;
};
