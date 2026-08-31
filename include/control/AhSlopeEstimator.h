#pragma once

#include <stdint.h>

// Least-squares slope of absolute humidity over a rolling window of samples
// collected while the chamber is sealed. The slope (g/m^3 per hour) is the
// moisture-release rate of the filament and the primary dryness signal: it
// decays towards the sensor noise floor as the spool dries out.
class AhSlopeEstimator {
 public:
  void reset();
  void addSample(uint32_t timeMs, float ahGm3);
  uint8_t sampleCount() const { return count_; }
  bool slopeValid() const;
  // g/m^3 per hour; 0.0 when there is not enough data yet.
  float slopeGm3PerHour() const;

 private:
  static constexpr uint8_t kCapacity = 40;
  float seconds_[kCapacity];
  float values_[kCapacity];
  uint8_t count_ = 0;
  uint8_t head_ = 0;  // index of the next write
};
