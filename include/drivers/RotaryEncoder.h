#pragma once

#include <ESP32Encoder.h>

#include "domain/Interfaces.h"

class RotaryEncoderInput final : public IInput {
 public:
  RotaryEncoderInput(uint8_t pinA, uint8_t pinB, uint8_t buttonPin);
  bool begin() override;
  void update(uint32_t now) override;
  int32_t encoderDelta() override;
  bool shortPress() override;
  bool longPress() override;

 private:
  ESP32Encoder encoder_;
  uint8_t pinA_;
  uint8_t pinB_;
  uint8_t buttonPin_;
  int64_t previousCount_ = 0;
  int32_t delta_ = 0;
  bool previousPressed_ = false;
  bool longReported_ = false;
  uint32_t pressedAt_ = 0;
  bool shortPressPending_ = false;
  bool longPressPending_ = false;
};
