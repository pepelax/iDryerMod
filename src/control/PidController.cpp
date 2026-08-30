#include "control/PidController.h"

#include <algorithm>

namespace {
float clampValue(float value, float low, float high) {
  return std::max(low, std::min(high, value));
}
}  // namespace

PidController::PidController() = default;

void PidController::begin(const PidConfig& config, uint32_t sampleTimeMs) {
  setTunings(config);
  sampleTimeMs_ = std::max<uint32_t>(1, sampleTimeMs);
  reset();
  setAutomatic(true);
}

void PidController::setTunings(const PidConfig& config) {
  kp_ = config.kp;
  ki_ = config.ki;
  kd_ = config.kd;
  outputMin_ = config.outputMin;
  outputMax_ = std::max(config.outputMin, config.outputMax);
  output_ = clampValue(output_, outputMin_, outputMax_);
  integral_ = clampValue(integral_, outputMin_, outputMax_);
}

void PidController::setAutomatic(bool enabled) {
  if (enabled && !automatic_) {
    integral_ = output_;
    previousInput_ = input_;
    firstUpdate_ = true;
  }
  automatic_ = enabled;
}

void PidController::reset(float output) {
  output_ = clampValue(output, outputMin_, outputMax_);
  integral_ = output_;
  previousInput_ = input_;
  lastUpdate_ = 0;
  firstUpdate_ = true;
}

float PidController::compute(float input, float setpoint, uint32_t now) {
  input_ = input;
  if (!automatic_) return output_;
  if (!firstUpdate_ && static_cast<uint32_t>(now - lastUpdate_) < sampleTimeMs_) {
    return output_;
  }
  const float dt = firstUpdate_
                       ? static_cast<float>(sampleTimeMs_) / 1000.0f
                       : static_cast<float>(now - lastUpdate_) / 1000.0f;
  const float error = setpoint - input_;
  const float inputDelta = firstUpdate_ ? 0.0f : input_ - previousInput_;
  integral_ = clampValue(integral_ + ki_ * error * dt, outputMin_, outputMax_);
  const float derivative = dt > 0.0f ? -kd_ * inputDelta / dt : 0.0f;
  output_ = clampValue(kp_ * error + integral_ + derivative, outputMin_, outputMax_);
  if ((output_ >= outputMax_ && error > 0.0f) ||
      (output_ <= outputMin_ && error < 0.0f)) {
    integral_ = clampValue(output_ - kp_ * error - derivative, outputMin_, outputMax_);
  }
  previousInput_ = input_;
  lastUpdate_ = now;
  firstUpdate_ = false;
  return output_;
}
