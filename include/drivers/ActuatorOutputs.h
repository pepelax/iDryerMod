#pragma once

#include <ESP32Servo.h>

#include "domain/Interfaces.h"

class HeaterOutput final : public IHeaterOutput {
 public:
  HeaterOutput(uint8_t pin, uint32_t windowMs);
  void begin() override;
  void setPower(float percent, uint32_t now) override;
  void off() override;

 private:
  uint8_t pin_;
  uint32_t windowMs_;
  uint32_t windowStartedAt_ = 0;
  float power_ = 0.0f;
};

class FanOutput final : public IFanOutput {
 public:
  FanOutput(uint8_t pin, uint8_t channel, uint16_t frequency, uint8_t resolution);
  void begin() override;
  void setPower(uint8_t percent) override;
  void off() override;

 private:
  uint8_t pin_;
  uint8_t channel_;
  uint16_t frequency_;
  uint8_t resolution_;
};

class ServoVentOutput final : public IVentOutput {
 public:
  ServoVentOutput(uint8_t pin, uint16_t minUs, uint16_t maxUs,
                  uint8_t movementThresholdDegrees, uint32_t releaseDelayMs,
                  uint8_t closedAngle, uint8_t openAngle);
  void begin() override;
  void setAngle(uint16_t angle) override;

 private:
  void attach();
  void releaseIfReady(uint32_t now);

  Servo servo_;
  uint8_t pin_;
  uint16_t minUs_;
  uint16_t maxUs_;
  uint8_t movementThresholdDegrees_;
  uint32_t releaseDelayMs_;
  uint8_t closedAngle_;
  uint8_t openAngle_;
  uint16_t lastDrivenAngle_ = 0;
  uint16_t lastRequestedAngle_ = 0;
  uint32_t commandStartedAt_ = 0;
  bool hasDrivenAngle_ = false;
  bool hasRequestedAngle_ = false;
  bool attached_ = false;
};
