#pragma once

#include <stddef.h>
#include <stdint.h>

struct PidConfig {
  float kp;
  float ki;
  float kd;
  float outputMin;
  float outputMax;
};

struct VentilationProfile {
  uint8_t minimumFan;
  uint8_t maximumFan;
  uint8_t closedAngle;
  uint8_t openAngle;
  uint32_t purgePeriodSeconds;
  uint32_t purgeDurationSeconds;
};

// Dry-to-completion preset: active drying runs until the spool is dry (the
// sealed-window AH slope stays below drynessSlopeGm3PerHour for
// kDrynessStableWindows consecutive windows after minDurationSeconds), then
// the chamber drops to holdTemperatureC and keeps watching. maxDurationSeconds
// is the safety ceiling. All timings/thresholds are empirical first guesses -
// recalibrate from logged slope data.
struct DryingPreset {
  char id[16];
  char name[24];
  float airTemperatureC;
  float holdTemperatureC;
  float relativeHumidity;
  float drynessSlopeGm3PerHour;
  uint32_t minDurationSeconds;
  uint32_t maxDurationSeconds;
  float heaterMaxTemperatureC;
  VentilationProfile ventilation;
};

struct AppConfig {
  char wifiSsid[33];
  char wifiPassword[65];
  char hostname[32];
  // Web panel credentials. An empty password disables authentication;
  // the login falls back to "admin" when left empty.
  char webLogin[33];
  char webPassword[65];

  float ntcR25Ohms;
  float ntcBeta;
  float ntcDividerOhms;
  float heaterMaxTemperatureC;
  float airMaxTemperatureC;
  float fanMinimumDuty;
  float fanStartupDuty;
  uint16_t servoMinUs;
  uint16_t servoMaxUs;
  uint8_t safeVentAngle;

  PidConfig temperaturePid;
  PidConfig humidityPid;
  float weightScaleOne;
  float weightScaleTwo;
  int32_t weightTareOne;
  int32_t weightTareTwo;
};

void setDefaultConfig(AppConfig& config);
const DryingPreset* defaultPresets(size_t& count);
