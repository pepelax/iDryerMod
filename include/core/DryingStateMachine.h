#pragma once

#include <stdint.h>

#include "domain/Types.h"

class DryingStateMachine {
 public:
  void begin(DeviceState& state, uint32_t now);
  bool start(DeviceState& state, DryingMode mode, const Setpoints& setpoints, uint32_t now);
  void pause(DeviceState& state, uint32_t now);
  void resume(DeviceState& state, uint32_t now);
  void stop(DeviceState& state, uint32_t now);
  void fault(DeviceState& state, FaultCode code, uint32_t now);
  void update(DeviceState& state, uint32_t now);

 private:
  uint32_t durationSeconds_ = 0;
  uint32_t pausedAt_ = 0;
  DryingPhase pausedFrom_ = DryingPhase::Idle;
};
