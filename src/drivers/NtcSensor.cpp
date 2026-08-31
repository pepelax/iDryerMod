#include "drivers/NtcSensor.h"

#include <Arduino.h>
#include <cmath>

NtcSensor::NtcSensor(uint8_t pin, float r25Ohms, float beta, float dividerOhms)
    : pin_(pin), r25Ohms_(r25Ohms), beta_(beta), dividerOhms_(dividerOhms) {}

bool NtcSensor::begin() {
  pinMode(pin_, INPUT);
  analogSetPinAttenuation(pin_, ADC_11db);
  return true;
}

float NtcSensor::rawToCelsius(int raw) const {
  constexpr float kAdcMax = 4095.0f;
  if (raw <= 1 || raw >= 4094) return NAN;
  const float ratio = static_cast<float>(raw) / kAdcMax;
  const float resistance = dividerOhms_ * ratio / (1.0f - ratio);
  const float t0 = 25.0f + 273.15f;
  const float invT = (1.0f / t0) + std::log(resistance / r25Ohms_) / beta_;
  return 1.0f / invT - 273.15f;
}

bool NtcSensor::update(uint32_t now) {
  const int raw = analogRead(pin_);
  const float temperature = rawToCelsius(raw);
  reading_.raw = raw;
  reading_.temperatureC = temperature;
  reading_.timestamp = now;
  reading_.valid = std::isfinite(temperature) && temperature > -40.0f &&
                   temperature < 200.0f;
  return reading_.valid;
}
