#pragma once

#include <stdint.h>

#include "config/AppConfig.h"

enum class DryingMode : uint8_t {
  Idle,
  TimedPreset,
  TimedManual,
  Continuous,
  Cooldown,
  Calibration,
  Fault
};

enum class DryingPhase : uint8_t {
  Idle,
  Precheck,
  Warmup,
  Drying,
  Paused,
  Finish,
  Cooldown,
  Fault
};

enum class DisplayView : uint8_t {
  Dashboard,
  ModeMenu
};

enum class FaultCode : uint8_t {
  None,
  NtcInvalid,
  HeaterOverTemperature,
  AirSensorInvalid,
  WeightSensorOneInvalid,
  WeightSensorTwoInvalid,
  WarmupTimeout,
  ConfigurationInvalid,
  WatchdogReset
};

struct AirReading {
  float temperatureC = 0.0f;
  float relativeHumidity = 0.0f;
  float absoluteHumidityGm3 = 0.0f;
  bool valid = false;
  uint32_t timestamp = 0;
};

struct HeaterReading {
  int raw = 0;
  float temperatureC = 0.0f;
  bool valid = false;
  uint32_t timestamp = 0;
};

struct WeightReading {
  int32_t raw = 0;
  float grams = 0.0f;
  bool valid = false;
  uint32_t timestamp = 0;
};

struct ActuatorState {
  float heaterPower = 0.0f;
  uint8_t fanPower = 0;
  uint16_t ventAngle = 0;
};

struct Setpoints {
  float airTemperatureC = 0.0f;
  float relativeHumidity = 0.0f;
  float heaterLimitC = 0.0f;
  uint32_t durationSeconds = 0;
};

struct DeviceState {
  AirReading air;
  HeaterReading heater;
  WeightReading spoolOne;
  WeightReading spoolTwo;
  ActuatorState actuators;
  Setpoints setpoints;
  DryingMode mode = DryingMode::Idle;
  DryingPhase phase = DryingPhase::Idle;
  FaultCode fault = FaultCode::None;
  uint32_t phaseStartedAt = 0;
  uint32_t runStartedAt = 0;
  uint32_t remainingSeconds = 0;
  bool wifiConnected = false;
  bool otaInProgress = false;
};

struct EventRecord {
  uint32_t timestamp;
  char type[24];
  char message[96];
};
