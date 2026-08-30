#include "services/SensorService.h"

#include "config/Defaults.h"

SensorService::SensorService(IAirSensor& air, IHeaterSensor& heater,
                             IWeightSensor& spoolOne, IWeightSensor& spoolTwo)
    : air_(air), heater_(heater), spoolOne_(spoolOne), spoolTwo_(spoolTwo) {}

bool SensorService::begin() {
  const bool airOk = air_.begin();
  const bool heaterOk = heater_.begin();
  const bool spoolOneOk = spoolOne_.begin();
  const bool spoolTwoOk = spoolTwo_.begin();
  return airOk && heaterOk && spoolOneOk && spoolTwoOk;
}

void SensorService::update(DeviceState& state, uint32_t now) {
  if (lastAirUpdate_ == 0 ||
      static_cast<uint32_t>(now - lastAirUpdate_) >= defaults::kAirSensorPeriodMs) {
    air_.update(now);
    lastAirUpdate_ = now;
  }
  if (lastHeaterUpdate_ == 0 ||
      static_cast<uint32_t>(now - lastHeaterUpdate_) >= defaults::kNtcPeriodMs) {
    heater_.update(now);
    lastHeaterUpdate_ = now;
  }
  if (lastWeightUpdate_ == 0 ||
      static_cast<uint32_t>(now - lastWeightUpdate_) >= defaults::kWeightPeriodMs) {
    spoolOne_.update(now);
    spoolTwo_.update(now);
    lastWeightUpdate_ = now;
  }
  state.air = air_.reading();
  state.heater = heater_.reading();
  state.spoolOne = spoolOne_.reading();
  state.spoolTwo = spoolTwo_.reading();
}
