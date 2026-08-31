#pragma once

#include "domain/Interfaces.h"

class ActuatorService {
 public:
  ActuatorService(IHeaterOutput& heater, IFanOutput& fan, IVentOutput& vent);
  void begin(uint16_t safeAngle);
  void apply(const ActuatorState& state, uint32_t now);
  void safe(uint16_t safeAngle);

 private:
  IHeaterOutput& heater_;
  IFanOutput& fan_;
  IVentOutput& vent_;
};
