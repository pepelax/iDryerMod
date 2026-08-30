#pragma once

#include <WebServer.h>

#include "config/AppConfig.h"
#include "core/DryingStateMachine.h"
#include "domain/Interfaces.h"

class WebService {
 public:
  WebService(DeviceState& state, AppConfig& config,
             DryingStateMachine& stateMachine, IStorage& storage);
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
  void saveConfig();
  void sendHistory();
  void sendEvents();
  void appendEvent(const char* type, const char* message);

  WebServer server_{80};
  DeviceState& state_;
  AppConfig& config_;
  DryingStateMachine& stateMachine_;
  IStorage& storage_;
};
