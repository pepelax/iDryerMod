#include "services/ActuatorService.h"

ActuatorService::ActuatorService(IHeaterOutput& heater, IFanOutput& fan, IVentOutput& vent)
    : heater_(heater), fan_(fan), vent_(vent) {}

void ActuatorService::begin(uint16_t safeAngle, uint8_t heaterFanFloor) {
  heaterFanFloor_ = heaterFanFloor > 100 ? 100 : heaterFanFloor;
  heater_.begin();
  fan_.begin();
  vent_.begin();
  safe(safeAngle);
}

void ActuatorService::apply(ActuatorState& state, uint32_t now) {
  if (state.heaterPower > 0.0f && state.fanPower < heaterFanFloor_) {
    state.fanPower = heaterFanFloor_;
  }
  fan_.setPower(state.fanPower);
  vent_.setAngle(state.ventAngle);
  heater_.setPower(state.heaterPower, now);
}

void ActuatorService::safe(uint16_t safeAngle) {
  heater_.off();
  fan_.off();
  vent_.setAngle(safeAngle);
}
