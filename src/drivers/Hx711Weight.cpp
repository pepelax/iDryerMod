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
  // Median of the window: unlike a plain average it rejects single-sample
  // spikes, so the idle reading stops flickering.
  int32_t sorted[8];
  for (uint8_t i = 0; i < sampleCount_; ++i) sorted[i] = samples_[i];
  for (uint8_t i = 1; i < sampleCount_; ++i) {
    const int32_t key = sorted[i];
    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = key;
  }
  int32_t filtered;
  if (sampleCount_ == 0) {
    filtered = raw;
  } else if (sampleCount_ % 2 == 1) {
    filtered = sorted[sampleCount_ / 2];
  } else {
    filtered = (sorted[sampleCount_ / 2 - 1] + sorted[sampleCount_ / 2]) / 2;
  }
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
