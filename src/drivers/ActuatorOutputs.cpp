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

ServoVentOutput::ServoVentOutput(uint8_t pin, uint16_t minUs, uint16_t maxUs,
                                 uint8_t movementThresholdDegrees,
                                 uint32_t releaseDelayMs, uint8_t closedAngle,
                                 uint8_t openAngle)
    : pin_(pin),
      minUs_(minUs),
      maxUs_(maxUs),
      movementThresholdDegrees_(movementThresholdDegrees),
      releaseDelayMs_(releaseDelayMs),
      closedAngle_(closedAngle),
      openAngle_(openAngle) {}

void ServoVentOutput::begin() {
  servo_.setPeriodHertz(50);
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void ServoVentOutput::setAngle(uint16_t angle) {
  const uint32_t now = millis();
  releaseIfReady(now);

  const uint16_t target = std::min<uint16_t>(180, angle);
  const bool requestChanged =
      !hasRequestedAngle_ || target != lastRequestedAngle_;
  lastRequestedAngle_ = target;
  hasRequestedAngle_ = true;

  const uint16_t delta = !hasDrivenAngle_
                             ? 180
                             : (target > lastDrivenAngle_
                                    ? target - lastDrivenAngle_
                                    : lastDrivenAngle_ - target);
  const bool extreme = target == closedAngle_ || target == openAngle_;
  if (hasDrivenAngle_ && delta < movementThresholdDegrees_ &&
      !(extreme && requestChanged)) {
    return;
  }
  if (hasDrivenAngle_ && delta == 0 && !requestChanged) return;

  attach();
  if (!attached_) return;
  servo_.write(target);
  lastDrivenAngle_ = target;
  hasDrivenAngle_ = true;
  commandStartedAt_ = now;
  Serial.printf("[servo] move to %u deg (delta=%u)%s\n", target, delta,
                extreme ? " extreme" : "");
}

void ServoVentOutput::attach() {
  if (attached_) return;
  servo_.setPeriodHertz(50);
  servo_.attach(pin_, minUs_, maxUs_);
  attached_ = servo_.attached();
  if (!attached_) Serial.println("[servo] attach FAILED");
}

void ServoVentOutput::releaseIfReady(uint32_t now) {
  if (!attached_ ||
      static_cast<uint32_t>(now - commandStartedAt_) < releaseDelayMs_) {
    return;
  }
  servo_.detach();
  attached_ = false;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
  Serial.printf("[servo] PWM released at %u deg\n", lastDrivenAngle_);
}
