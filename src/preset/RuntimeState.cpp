#include "preset/RuntimeState.h"

namespace ardor {

void RuntimeState::observeRealtimeStats(uint64_t previousCallbacks,
                                        uint64_t currentCallbacks,
                                        uint64_t previousOverBudget,
                                        uint64_t currentOverBudget)
{
  const uint64_t callbackDelta = currentCallbacks - previousCallbacks;
  const uint64_t overDelta = currentOverBudget - previousOverBudget;
  const bool overloaded = callbackDelta > 0 && overDelta * 100 > callbackDelta * 5;

  if (overloaded) {
    ++consecutiveBadSeconds_;
    consecutiveStableSeconds_ = 0;
    if (consecutiveBadSeconds_ >= 3) {
      effectsBypassed_ = true;
    }
  } else {
    consecutiveBadSeconds_ = 0;
    ++consecutiveStableSeconds_;
    if (consecutiveStableSeconds_ >= 3) {
      effectsBypassed_ = false;
    }
  }
}

void RuntimeState::clearEffectsBypass()
{
  effectsBypassed_ = false;
  consecutiveBadSeconds_ = 0;
  consecutiveStableSeconds_ = 0;
}

void RuntimeState::changePreset()
{
  effectsBypassed_ = false;
  consecutiveBadSeconds_ = 0;
  consecutiveStableSeconds_ = 0;
}

bool RuntimeState::effectsBypassed() const
{
  return effectsBypassed_;
}

} // namespace ardor
