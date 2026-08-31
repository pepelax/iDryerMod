#pragma once

#include <stdint.h>

#include "config/AppConfig.h"
#include "domain/Types.h"

class IClock {
 public:
  virtual ~IClock() = default;
  virtual uint32_t now() const = 0;
};

class IAirSensor {
 public:
  virtual ~IAirSensor() = default;
  virtual bool begin() = 0;
  virtual bool update(uint32_t now) = 0;
  virtual AirReading reading() const = 0;
};

class IHeaterSensor {
 public:
  virtual ~IHeaterSensor() = default;
  virtual bool begin() = 0;
  virtual bool update(uint32_t now) = 0;
  virtual HeaterReading reading() const = 0;
};

class IWeightSensor {
 public:
  virtual ~IWeightSensor() = default;
  virtual bool begin() = 0;
  virtual bool update(uint32_t now) = 0;
  virtual WeightReading reading() const = 0;
  virtual void tare() = 0;
  virtual void setScale(float scale) = 0;
  virtual void setTareRaw(int32_t tareRaw) = 0;
  virtual int32_t tareRaw() const = 0;
};

class IDisplay {
 public:
  virtual ~IDisplay() = default;
  virtual bool begin() = 0;
  virtual void render(const DeviceState& state) = 0;
  virtual void render(const DeviceState& state, const UiState& ui) {
    (void)ui;
    render(state);
  }
};

class IInput {
 public:
  virtual ~IInput() = default;
  virtual bool begin() = 0;
  virtual void update(uint32_t now) = 0;
  virtual int32_t encoderDelta() = 0;
  virtual bool shortPress() = 0;
  virtual bool longPress() = 0;
};

class IStorage {
 public:
  virtual ~IStorage() = default;
  virtual bool begin() = 0;
  virtual bool loadConfig(AppConfig& config) = 0;
  virtual bool saveConfig(const AppConfig& config) = 0;
  virtual bool appendTelemetry(const DeviceState& state) = 0;
  virtual bool appendEvent(const EventRecord& event) = 0;
  virtual bool loadWeightCalibration(uint8_t spool, WeightCalTable& table) = 0;
  virtual bool saveWeightCalibration(uint8_t spool,
                                     const WeightCalTable& table) = 0;
};

class IHeaterOutput {
 public:
  virtual ~IHeaterOutput() = default;
  virtual void begin() = 0;
  virtual void setPower(float percent, uint32_t now) = 0;
  virtual void off() = 0;
};

class IFanOutput {
 public:
  virtual ~IFanOutput() = default;
  virtual void begin() = 0;
  virtual void setPower(uint8_t percent) = 0;
  virtual void off() = 0;
};

class IVentOutput {
 public:
  virtual ~IVentOutput() = default;
  virtual void begin() = 0;
  virtual void setAngle(uint16_t angle) = 0;
};
