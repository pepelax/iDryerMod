#include "control/AhSlopeEstimator.h"

#include "config/Defaults.h"

void AhSlopeEstimator::reset() {
  count_ = 0;
  head_ = 0;
}

void AhSlopeEstimator::addSample(uint32_t timeMs, float ahGm3) {
  seconds_[head_] = static_cast<float>(timeMs) * 0.001f;
  values_[head_] = ahGm3;
  head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
  if (count_ < kCapacity) ++count_;
}

bool AhSlopeEstimator::slopeValid() const {
  return count_ >= defaults::kAhSlopeMinSamples;
}

float AhSlopeEstimator::slopeGm3PerHour() const {
  if (!slopeValid()) return 0.0f;
  // Newest sample is at (head_ - 1); index i walks from oldest to newest.
  const uint8_t newest = static_cast<uint8_t>((head_ + kCapacity - 1) % kCapacity);
  const float tNewest = seconds_[newest];
  float n = 0.0f;
  float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
  for (uint8_t i = 0; i < count_; ++i) {
    const uint8_t idx =
        static_cast<uint8_t>((head_ + kCapacity - count_ + i) % kCapacity);
    // Seconds relative to the newest sample: keeps the numbers small so the
    // regression stays numerically stable even after hours of uptime.
    const float x = seconds_[idx] - tNewest;
    const float y = values_[idx];
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    n += 1.0f;
  }
  const float denominator = n * sxx - sx * sx;
  if (denominator < 1.0e-6f) return 0.0f;
  return (n * sxy - sx * sy) / denominator * 3600.0f;
}
