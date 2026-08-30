#pragma once

#include "domain/Interfaces.h"

class SafetyService {
 public:
  explicit SafetyService(float airMaxTemperatureC);
  void setAirMaxTemperature(float value) { airMaxTemperatureC_ = value; }
  bool evaluate(const DeviceState& state, uint32_t now, FaultCode& fault) const;
  bool canHeat(const DeviceState& state) const;

 private:
  float airMaxTemperatureC_;
};
