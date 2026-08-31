#pragma once

#include "config/Defaults.h"
#include "domain/Interfaces.h"

class ActuatorService {
 public:
  ActuatorService(IHeaterOutput& heater, IFanOutput& fan, IVentOutput& vent);
  void begin(uint16_t safeAngle, uint8_t heaterFanFloor);
  void apply(ActuatorState& state, uint32_t now);
  void safe(uint16_t safeAngle);

 private:
  IHeaterOutput& heater_;
  IFanOutput& fan_;
  IVentOutput& vent_;
  uint8_t heaterFanFloor_ = static_cast<uint8_t>(defaults::kActiveFanMinimumDuty);
};
