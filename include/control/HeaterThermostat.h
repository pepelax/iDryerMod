#pragma once

#include <stdint.h>

// Inner loop of the temperature cascade. A reverse-acting bang-bang
// thermostat keeps the heater (measured by the NTC) inside a band of
// +/-kHeaterThermostatHysteresisC/2 around the setpoint, so the MOSFET only
// ever switches fully on or fully off. The setpoint itself comes from the
// outer air-temperature PID, which keeps the heater just hot enough.
class HeaterThermostat {
 public:
  // Returns true while the heater should be powered.
  bool update(float heaterTempC, float setpointC, uint32_t now);
  void reset();
  bool output() const { return on_; }

 private:
  bool on_ = false;
  bool everSwitched_ = false;
  uint32_t lastSwitchAt_ = 0;
};

// Upper bound for the cascade setpoint: the stricter of the preset heater
// limit and the global config limit, backed off by kHeaterSetpointMarginC so
// thermostat overshoot never reaches the hard Fault threshold. A zero/unset
// bound falls back to the other one; returns 0 when both are unset.
float heaterSetpointCeilingC(float presetHeaterLimitC, float configHeaterMaxC);
