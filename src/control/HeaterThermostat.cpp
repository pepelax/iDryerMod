#include "control/HeaterThermostat.h"

#include <algorithm>

#include "config/Defaults.h"

bool HeaterThermostat::update(float heaterTempC, float setpointC, uint32_t now) {
  const float halfBand = defaults::kHeaterThermostatHysteresisC * 0.5f;
  const bool wantOn = heaterTempC < setpointC - halfBand;
  const bool wantOff = heaterTempC > setpointC + halfBand;
  const bool switchAllowed =
      !everSwitched_ ||
      static_cast<uint32_t>(now - lastSwitchAt_) >= defaults::kHeaterMinSwitchIntervalMs;
  if (switchAllowed && ((wantOn && !on_) || (wantOff && on_))) {
    on_ = wantOn;
    lastSwitchAt_ = now;
    everSwitched_ = true;
  }
  return on_;
}

void HeaterThermostat::reset() {
  on_ = false;
  everSwitched_ = false;
  lastSwitchAt_ = 0;
}

float heaterSetpointCeilingC(float presetHeaterLimitC, float configHeaterMaxC) {
  float limit = presetHeaterLimitC > 0.0f ? presetHeaterLimitC : 0.0f;
  if (configHeaterMaxC > 0.0f) {
    limit = limit > 0.0f ? std::min(limit, configHeaterMaxC) : configHeaterMaxC;
  }
  if (limit <= 0.0f) return 0.0f;
  return limit - defaults::kHeaterSetpointMarginC;
}
