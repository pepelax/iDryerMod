#include "control/PurgeScheduler.h"

void PurgeScheduler::begin(const PurgeParams& params, uint32_t now) {
  params_ = params;
  phase_ = PurgePhase::Sealed;
  phaseStartedAt_ = now;
  // Pretend the seal has already lasted its minimum so the very first purge
  // (flushing room air) can start as soon as the AH trigger fires.
  sealStartedAt_ = now - params.minSealMs;
  sealBaselineGm3_ = 0.0f;
}

PurgePhase PurgeScheduler::update(float absoluteHumidityGm3, float purgeFloorGm3,
                                  uint32_t now) {
  switch (phase_) {
    case PurgePhase::Sealed: {
      const bool sealedLongEnough =
          static_cast<uint32_t>(now - sealStartedAt_) >= params_.minSealMs;
      const bool roseEnough =
          absoluteHumidityGm3 - sealBaselineGm3_ >= params_.riseTriggerGm3;
      const bool saturated = absoluteHumidityGm3 >= purgeFloorGm3;
      if (sealedLongEnough && (roseEnough || saturated)) {
        phase_ = PurgePhase::Purge;
        phaseStartedAt_ = now;
      }
      break;
    }
    case PurgePhase::Purge: {
      const bool capReached =
          static_cast<uint32_t>(now - phaseStartedAt_) >= params_.maxPurgeMs;
      const bool cleared = absoluteHumidityGm3 <= purgeFloorGm3;
      if (capReached || cleared) {
        phase_ = PurgePhase::Settle;
        phaseStartedAt_ = now;
      }
      break;
    }
    case PurgePhase::Settle: {
      if (static_cast<uint32_t>(now - phaseStartedAt_) >= params_.settleMs) {
        phase_ = PurgePhase::Sealed;
        phaseStartedAt_ = now;
        sealStartedAt_ = now;
        sealBaselineGm3_ = absoluteHumidityGm3;
      }
      break;
    }
  }
  return phase_;
}
