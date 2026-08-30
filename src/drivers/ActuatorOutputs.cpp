#include "drivers/ActuatorOutputs.h"

#include <Arduino.h>
#include <algorithm>

HeaterOutput::HeaterOutput(uint8_t pin, uint32_t windowMs)
    : pin_(pin), windowMs_(windowMs) {}

void HeaterOutput::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void HeaterOutput::setPower(float percent, uint32_t now) {
  power_ = std::max(0.0f, std::min(100.0f, percent));
  if (power_ <= 0.0f) {
    digitalWrite(pin_, LOW);
    return;
  }
  if (power_ >= 100.0f) {
    digitalWrite(pin_, HIGH);
    return;
  }
  if (static_cast<uint32_t>(now - windowStartedAt_) >= windowMs_) {
    windowStartedAt_ = now;
  }
  const uint32_t elapsed = now - windowStartedAt_;
  const uint32_t onTime = static_cast<uint32_t>(windowMs_ * power_ / 100.0f);
  digitalWrite(pin_, elapsed < onTime ? HIGH : LOW);
}

void HeaterOutput::off() {
  power_ = 0.0f;
  digitalWrite(pin_, LOW);
}

FanOutput::FanOutput(uint8_t pin, uint8_t channel, uint16_t frequency, uint8_t resolution)
    : pin_(pin), channel_(channel), frequency_(frequency), resolution_(resolution) {}

void FanOutput::begin() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin_, frequency_, resolution_, channel_);
  ledcWriteChannel(channel_, 0);
#else
  ledcSetup(channel_, frequency_, resolution_);
  ledcAttachPin(pin_, channel_);
  ledcWrite(channel_, 0);
#endif
}

void FanOutput::setPower(uint8_t percent) {
  const uint32_t maxDuty = (1UL << resolution_) - 1UL;
  const uint32_t duty = maxDuty * std::min<uint8_t>(100, percent) / 100UL;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteChannel(channel_, duty);
#else
  ledcWrite(channel_, duty);
#endif
}

void FanOutput::off() {
  setPower(0);
}

ServoVentOutput::ServoVentOutput(uint8_t pin, uint16_t minUs, uint16_t maxUs)
    : pin_(pin), minUs_(minUs), maxUs_(maxUs) {}

void ServoVentOutput::begin() {
  servo_.setPeriodHertz(50);
  servo_.attach(pin_, minUs_, maxUs_);
  servo_.write(90);
}

void ServoVentOutput::setAngle(uint16_t angle) {
  servo_.write(std::min<uint16_t>(180, angle));
}
