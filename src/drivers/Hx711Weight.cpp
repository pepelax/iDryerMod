#include "drivers/Hx711Weight.h"

#include <Arduino.h>
#include <algorithm>

Hx711Weight::Hx711Weight(uint8_t doutPin, uint8_t sckPin)
    : doutPin_(doutPin), sckPin_(sckPin) {}

bool Hx711Weight::begin() {
  adc_.begin(doutPin_, sckPin_);
  pinMode(doutPin_, INPUT_PULLUP);
  adc_.set_gain(128);
  return true;
}

bool Hx711Weight::update(uint32_t now) {
  if (!adc_.is_ready()) {
    reading_.valid = false;
    return false;
  }
  const int32_t raw = static_cast<int32_t>(adc_.read());
  samples_[sampleIndex_] = raw;
  sampleIndex_ = (sampleIndex_ + 1) % 8;
  sampleCount_ = std::min<uint8_t>(sampleCount_ + 1, 8);
  int64_t sum = 0;
  for (uint8_t i = 0; i < sampleCount_; ++i) sum += samples_[i];
  const int32_t filtered = static_cast<int32_t>(sum / sampleCount_);
  reading_.raw = filtered;
  reading_.grams = static_cast<float>(filtered - tareRaw_) / scale_;
  reading_.timestamp = now;
  reading_.valid = true;
  return true;
}

void Hx711Weight::tare() {
  if (!adc_.is_ready()) return;
  tareRaw_ = static_cast<int32_t>(adc_.read_average(16));
  sampleCount_ = 0;
  sampleIndex_ = 0;
}
