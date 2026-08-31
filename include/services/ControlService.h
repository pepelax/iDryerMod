#pragma once

#include "config/AppConfig.h"
#include "control/AhSlopeEstimator.h"
#include "control/HeaterThermostat.h"
#include "control/PidController.h"
#include "control/PurgeScheduler.h"
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
  void restartPulseCycle(DeviceState& state, uint32_t now);

  const AppConfig& config_;
  SafetyService& safety_;
  DryingStateMachine& stateMachine_;
  PidController temperaturePid_;
  HeaterThermostat thermostat_;
  PurgeScheduler purge_;
  AhSlopeEstimator ahSlope_;
  DryingPhase lastPhase_ = DryingPhase::Idle;
  PurgePhase lastPurgePhase_ = PurgePhase::Sealed;
  float lastWindowSlope_ = 0.0f;
  bool lastWindowSlopeValid_ = false;
  uint8_t stableWindows_ = 0;
  uint32_t lastAhSampleAt_ = 0;
  float cascadeFloor_ = 0.0f;
  float cascadeCeiling_ = 0.0f;
};
