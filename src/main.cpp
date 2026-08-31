#ifndef PIO_UNIT_TESTING

#include <Arduino.h>
#include <Wire.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstring>

#include "config/AppConfig.h"
#include "config/BoardConfig.h"
#include "config/Defaults.h"
#include "core/DryingStateMachine.h"
#include "drivers/Aht30Sensor.h"
#include "drivers/ActuatorOutputs.h"
#include "drivers/Hx711Weight.h"
#include "drivers/NtcSensor.h"
#include "drivers/RotaryEncoder.h"
#include "drivers/Sh1106Display.h"
#include "services/ActuatorService.h"
#include "services/CalibrationService.h"
#include "services/ControlService.h"
#include "services/NetworkService.h"
#include "services/SafetyService.h"
#include "services/SensorService.h"
#include "services/StorageService.h"
#include "services/UiService.h"
#include "services/WebService.h"

namespace {
AppConfig appConfig;
DeviceState deviceState;
DryingStateMachine stateMachine;
StorageService storage;

Aht30Sensor airSensor(Wire, board::kAht30Address);
NtcSensor ntcSensor(board::kNtc, defaults::kNtcR25Ohms, defaults::kNtcBeta,
                    defaults::kNtcDividerOhms);
Hx711Weight weightOne(board::kHx711OneDout, board::kHx711OneSck);
Hx711Weight weightTwo(board::kHx711TwoDout, board::kHx711TwoSck);
SensorService sensors(airSensor, ntcSensor, weightOne, weightTwo);

HeaterOutput heaterOutput(board::kHeater, defaults::kHeaterWindowMs);
FanOutput fanOutput(board::kFan, board::kFanPwmChannel, board::kFanPwmFrequency,
                   board::kFanPwmResolution);
ServoVentOutput ventOutput(
    board::kServo, defaults::kServoMinUs, defaults::kServoMaxUs,
    defaults::kServoMovementThresholdDegrees, defaults::kServoReleaseDelayMs,
    defaults::kServoClosedAngle, defaults::kServoOpenAngle);
ActuatorService actuators(heaterOutput, fanOutput, ventOutput);
SafetyService safety(defaults::kAirMaxTemperatureC);
ControlService control(appConfig, safety, stateMachine);
CalibrationService calibration(appConfig, stateMachine, storage, weightOne,
                               weightTwo);
Sh1106Display display(board::kDisplayAddress);
RotaryEncoderInput input(board::kEncoderA, board::kEncoderB, board::kButton);
UiService ui(display, input, stateMachine, calibration, appConfig);
NetworkService network(appConfig);
WebService web(deviceState, appConfig, stateMachine, storage, calibration);

uint32_t lastSensors = 0;
uint32_t lastControl = 0;
uint32_t lastHistory = 0;
uint32_t lastDiagnostics = 0;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n[iDryer] firmware %s\n", APP_VERSION);
  Serial.printf("[boot] flash=%u bytes, heap=%u bytes\n", ESP.getFlashChipSize(),
                ESP.getFreeHeap());
  setDefaultConfig(appConfig);
  const bool storageOk = storage.begin();
  storage.loadConfig(appConfig);
  // Migrate configs saved before the mDNS name was shortened; a custom
  // hostname chosen by the user is left untouched.
  if (std::strcmp(appConfig.hostname, "filament-dryer") == 0) {
    strncpy(appConfig.hostname, "dryer", sizeof(appConfig.hostname) - 1);
    appConfig.hostname[sizeof(appConfig.hostname) - 1] = '\0';
  }
  weightOne.setScale(appConfig.weightScaleOne);
  weightTwo.setScale(appConfig.weightScaleTwo);
  weightOne.setTareRaw(appConfig.weightTareOne);
  weightTwo.setTareRaw(appConfig.weightTareTwo);
  calibration.begin();
  safety.setAirMaxTemperature(appConfig.airMaxTemperatureC);
  stateMachine.begin(deviceState, millis());
  deviceState.actuators.ventAngle = appConfig.safeVentAngle;
  control.begin();

  Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cFrequency);
  const float heaterFanFloor = std::min(
      100.0f, std::max(appConfig.fanMinimumDuty, defaults::kActiveFanMinimumDuty));
  actuators.begin(appConfig.safeVentAngle,
                  static_cast<uint8_t>(heaterFanFloor));
  const bool sensorsOk = sensors.begin();
  const bool uiOk = ui.begin();
  const bool networkOk = network.begin(deviceState);
  const bool webOk = web.begin();
  Serial.printf("[boot] storage=%s sensors=%s ui=%s network=%s web=%s\n",
                storageOk ? "OK" : "FAILED", sensorsOk ? "OK" : "MISSING",
                uiOk ? "OK" : "MISSING", networkOk ? "OK" : "FAILED",
                webOk ? "OK" : "FAILED");
  Serial.println("[boot] control loop started; Wi-Fi is optional");

#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdtConfig = {
      .timeout_ms = defaults::kWatchdogTimeoutSeconds * 1000,
      .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
      .trigger_panic = true};
  esp_task_wdt_init(&wdtConfig);
#else
  esp_task_wdt_init(defaults::kWatchdogTimeoutSeconds, true);
#endif
  esp_task_wdt_add(nullptr);
}

void loop() {
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSensors) >= defaults::kWeightPeriodMs) {
    sensors.update(deviceState, now);
    lastSensors = now;
  }
  if (static_cast<uint32_t>(now - lastControl) >= defaults::kControlPeriodMs) {
    control.update(deviceState, now);
    // Calibration runs after the control loop: it compensates the displayed
    // weights and forces cooling airflow during the drift cool-down phase.
    calibration.update(deviceState, now);
    actuators.apply(deviceState.actuators, now);
    lastControl = now;
  }
  ui.update(deviceState, now);
  if (static_cast<uint32_t>(now - lastHistory) >= defaults::kHistoryPeriodMs &&
      !deviceState.otaInProgress) {
    const bool historyOk = storage.appendTelemetry(deviceState);
    Serial.printf("[storage] telemetry append: %s\n", historyOk ? "OK" : "FAILED");
    lastHistory = now;
  }
  if (static_cast<uint32_t>(now - lastDiagnostics) >=
      defaults::kDiagnosticsPeriodMs) {
    Serial.printf(
        "[sensors] AIR temp=%.2fC RH=%.2f%% abs=%.2fg/m3 valid=%u | "
        "NTC raw=%d temp=%.2fC valid=%u | CTRL heatSp=%.1fC heat=%.0f%% | "
        "PURGE phase=%u slope=%.2fg/m3h n=%u | "
        "HX1 raw=%ld valid=%u | "
        "HX2 raw=%ld valid=%u | ENC A=%d B=%d SW=%d\n",
        deviceState.air.temperatureC, deviceState.air.relativeHumidity,
        deviceState.air.absoluteHumidityGm3, deviceState.air.valid,
        deviceState.heater.raw, deviceState.heater.temperatureC,
        deviceState.heater.valid, deviceState.heaterSetpointC,
        deviceState.actuators.heaterPower, deviceState.purgePhase,
        deviceState.ahSlopeGm3PerHour, deviceState.ahSlopeSamples,
        static_cast<long>(deviceState.spoolOne.raw),
        deviceState.spoolOne.valid, static_cast<long>(deviceState.spoolTwo.raw),
        deviceState.spoolTwo.valid, digitalRead(board::kEncoderA),
        digitalRead(board::kEncoderB), digitalRead(board::kButton));
    lastDiagnostics = now;
  }
  network.update(deviceState, now);
  web.update();
  esp_task_wdt_reset();
  delay(1);
}

#endif  // PIO_UNIT_TESTING
