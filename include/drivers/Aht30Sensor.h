#pragma once

#include <Adafruit_AHTX0.h>

#include "domain/Interfaces.h"

class Aht30Sensor final : public IAirSensor {
 public:
  Aht30Sensor(TwoWire& wire, uint8_t address);
  bool begin() override;
  bool update(uint32_t now) override;
  AirReading reading() const override { return reading_; }

 private:
  TwoWire& wire_;
  uint8_t address_;
  Adafruit_AHTX0 sensor_;
  AirReading reading_;
};
