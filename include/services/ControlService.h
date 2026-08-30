#pragma once

#include "config/AppConfig.h"
#include "control/PidController.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"
#include "services/SafetyService.h"

class ControlService {
 public:
  ControlService(const AppConfig& config, SafetyService& safety,
                 DryingStateMachine& stateMachine);
  void begin();
  void update(DeviceState& state, uint32_t now);

 private:
  const AppConfig& config_;
  SafetyService& safety_;
  DryingStateMachine& stateMachine_;
  PidController temperaturePid_;
  PidController humidityPid_;
};
