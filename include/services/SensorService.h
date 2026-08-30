#pragma once

#include "domain/Interfaces.h"

class SensorService {
 public:
  SensorService(IAirSensor& air, IHeaterSensor& heater,
                IWeightSensor& spoolOne, IWeightSensor& spoolTwo);
  bool begin();
  void update(DeviceState& state, uint32_t now);

 private:
  IAirSensor& air_;
  IHeaterSensor& heater_;
  IWeightSensor& spoolOne_;
  IWeightSensor& spoolTwo_;
  uint32_t lastAirUpdate_ = 0;
  uint32_t lastHeaterUpdate_ = 0;
  uint32_t lastWeightUpdate_ = 0;
};
