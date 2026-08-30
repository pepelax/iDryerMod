#pragma once

#include <stdint.h>

#include "config/AppConfig.h"

class PidController {
 public:
  PidController();
  void begin(const PidConfig& config, uint32_t sampleTimeMs);
  void setTunings(const PidConfig& config);
  void setAutomatic(bool enabled);
  void reset(float output = 0.0f);
  float compute(float input, float setpoint, uint32_t now);
  float output() const { return output_; }

 private:
  float input_ = 0.0f;
  float output_ = 0.0f;
  float kp_ = 0.0f;
  float ki_ = 0.0f;
  float kd_ = 0.0f;
  float outputMin_ = 0.0f;
  float outputMax_ = 100.0f;
  float integral_ = 0.0f;
  float previousInput_ = 0.0f;
  uint32_t lastUpdate_ = 0;
  uint32_t sampleTimeMs_ = 1000;
  bool automatic_ = false;
  bool firstUpdate_ = true;
};
