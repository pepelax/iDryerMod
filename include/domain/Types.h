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

enum class UiScreen : uint8_t {
  Dashboard,
  MainMenu,
  ManualSetup,
  ContinuousSetup,
  CalibrationMenu,
  ScaleSetup,
  DriftConfirm
};

// Temperature compensation band count (see defaults::kWeightCalBandWidthC).
enum : uint8_t { kWeightCalBands = 16 };

// Temperature compensation table for one load cell: a multiplier per 5 °C
// band measured during a heat/cool drift calibration run. coeff[i] corrects
// the raw reading in band i back to the reference (room) temperature.
struct WeightCalTable {
  bool valid = false;
  uint8_t filledBands = 0;
  float coeff[kWeightCalBands];

  WeightCalTable() {
    for (uint8_t i = 0; i < kWeightCalBands; ++i) coeff[i] = 1.0f;
  }
};

// Live status of a drift calibration session.
struct CalibrationInfo {
  bool active = false;
  uint8_t phase = 0;  // 0 idle, 1 heat, 2 cool
  uint16_t points = 0;
  float targetTempC = 0.0f;
  float startTempC = 0.0f;
};

// Snapshot of the local menu the UiService wants on the display. The display
// derives row labels itself so this stays small enough to copy by value.
struct UiState {
  UiScreen screen = UiScreen::Dashboard;
  uint8_t cursor = 0;        // selected row within the current screen
  uint8_t scrollOffset = 0;  // first visible row for scrolling lists
  uint8_t editField = 0;     // 0 = navigation, 1/2 = field being edited
  float manualTemperatureC = 45.0f;
  uint32_t manualDurationSeconds = 6UL * 3600UL;
  float continuousTemperatureC = 45.0f;
  uint8_t targetSpool = 1;      // scale-calibration target: 1 or 2
  uint16_t knownGrams = 1000;   // reference mass for scale calibration
  // Short transient message ("TARED", "SAVED"...); empty when nothing to show.
  char toast[20] = "";
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
  // Human-readable tag of the active run ("PLA", "MANUAL", "CONTINUOUS"...).
  char runLabel[20] = "";
  uint32_t phaseStartedAt = 0;
  uint32_t runStartedAt = 0;
  uint32_t remainingSeconds = 0;
  bool wifiConnected = false;
  bool apActive = false;
  // Current station or AP address for the web panel, empty when offline.
  char ipAddress[16] = "";
  // Friendlier address to show: "dryer.local" once mDNS is up, otherwise
  // falls back to the raw IP.
  char webAddress[24] = "";
  CalibrationInfo calibration;
  bool otaInProgress = false;
};

struct EventRecord {
  uint32_t timestamp;
  char type[24];
  char message[96];
};
