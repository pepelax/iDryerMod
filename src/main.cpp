#ifndef PIO_UNIT_TESTING

#include <Arduino.h>
#include <Wire.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>

#include "config/AppConfig.h"
#include "config/BoardConfig.h"
#include "config/Defaults.h"
#include "core/DryingStateMachine.h"
#include "drivers/Aht30Sensor.h"
#include "drivers/ActuatorOutputs.h"
#include "drivers/Hx711Weight.h"
#include "drivers/NtcSensor.h"
#include "drivers/RotaryEncoder.h"
#include "drivers/Ssd1306Display.h"
#include "services/ActuatorService.h"
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
ServoVentOutput ventOutput(board::kServo, defaults::kServoMinUs, defaults::kServoMaxUs);
ActuatorService actuators(heaterOutput, fanOutput, ventOutput);
SafetyService safety(defaults::kAirMaxTemperatureC);
ControlService control(appConfig, safety, stateMachine);
Ssd1306Display display(board::kDisplayAddress);
RotaryEncoderInput input(board::kEncoderA, board::kEncoderB, board::kButton);
UiService ui(display, input, stateMachine);
NetworkService network(appConfig);
WebService web(deviceState, appConfig, stateMachine, storage);

uint32_t lastSensors = 0;
uint32_t lastControl = 0;
uint32_t lastHistory = 0;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  setDefaultConfig(appConfig);
  storage.begin();
  storage.loadConfig(appConfig);
  weightOne.setScale(appConfig.weightScaleOne);
  weightTwo.setScale(appConfig.weightScaleTwo);
  weightOne.setTareRaw(appConfig.weightTareOne);
  weightTwo.setTareRaw(appConfig.weightTareTwo);
  safety.setAirMaxTemperature(appConfig.airMaxTemperatureC);
  stateMachine.begin(deviceState, millis());
  control.begin();

  Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cFrequency);
  actuators.begin();
  sensors.begin();
  ui.begin();
  network.begin(deviceState);
  web.begin();

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
    actuators.apply(deviceState.actuators, now);
    lastControl = now;
  }
  ui.update(deviceState, now);
  if (static_cast<uint32_t>(now - lastHistory) >= defaults::kHistoryPeriodMs) {
    storage.appendTelemetry(deviceState);
    lastHistory = now;
  }
  network.update(deviceState, now);
  web.update();
  esp_task_wdt_reset();
  delay(1);
}

#endif  // PIO_UNIT_TESTING
