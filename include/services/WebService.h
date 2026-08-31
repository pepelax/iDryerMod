#pragma once

#include <WebServer.h>

#include "config/AppConfig.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"
#include "services/CalibrationService.h"

class WebService {
 public:
  WebService(DeviceState& state, AppConfig& config,
             DryingStateMachine& stateMachine, IStorage& storage,
             CalibrationService& calibration);
  bool begin();
  void update();

 private:
  bool authorize();
  void sendState();
  void sendConfig();
  void runCommand();
  void stopCommand();
  void pauseCommand();
  void calibrationCommand();
  void sendCalibration();
  void saveConfig();
  void sendHistory();
  void sendEvents();
  void scanCommand();
  void rebootCommand();
  void appendEvent(const char* type, const char* message);

  WebServer server_{80};
  DeviceState& state_;
  AppConfig& config_;
  DryingStateMachine& stateMachine_;
  IStorage& storage_;
  CalibrationService& calibration_;
  // Per-boot random token; a browser session cookie with it replaces
  // repeated Basic-auth round trips for fetch() calls.
  String sessionToken_;
  bool scanRunning_ = false;
};
