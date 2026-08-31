#pragma once

#include "config/AppConfig.h"
#include "domain/Types.h"

class NetworkService {
 public:
  explicit NetworkService(const AppConfig& config);
  bool begin(DeviceState& state);
  void update(DeviceState& state, uint32_t now);

 private:
  const AppConfig& config_;
  uint32_t lastReconnectAt_ = 0;
  bool mdnsStarted_ = false;
  bool connectionStateKnown_ = false;
  bool wasConnected_ = false;
};
