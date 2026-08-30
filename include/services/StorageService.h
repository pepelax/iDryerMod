#pragma once

#include <Preferences.h>

#include "domain/Interfaces.h"

class StorageService final : public IStorage {
 public:
  bool begin() override;
  bool loadConfig(AppConfig& config) override;
  bool saveConfig(const AppConfig& config) override;
  bool appendTelemetry(const DeviceState& state) override;
  bool appendEvent(const EventRecord& event) override;

 private:
  bool rotateIfNeeded(const char* path);
  Preferences preferences_;
};
