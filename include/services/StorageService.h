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
  bool loadWeightCalibration(uint8_t spool, WeightCalTable& table) override;
  bool saveWeightCalibration(uint8_t spool,
                             const WeightCalTable& table) override;

 private:
  bool rotateIfNeeded(const char* path);
  // Converts the firmware-0.1.0 binary config blob ("config" key) into the
  // key-value format so settings survive struct layout changes.
  void migrateLegacyBlob(AppConfig& config);
  Preferences preferences_;
};
