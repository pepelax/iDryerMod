#include "services/ActuatorService.h"

ActuatorService::ActuatorService(IHeaterOutput& heater, IFanOutput& fan, IVentOutput& vent)
    : heater_(heater), fan_(fan), vent_(vent) {}

void ActuatorService::begin() {
  heater_.begin();
  fan_.begin();
  vent_.begin();
  safe(90);
}

void ActuatorService::apply(const ActuatorState& state, uint32_t now) {
  heater_.setPower(state.heaterPower, now);
  fan_.setPower(state.fanPower);
  vent_.setAngle(state.ventAngle);
}

void ActuatorService::safe(uint16_t safeAngle) {
  heater_.off();
  fan_.off();
  vent_.setAngle(safeAngle);
}
