#include "preset/RuntimeState.h"

namespace ardor {

void RuntimeState::reportOverload()
{
  if (effectsBypassed_) {
    return;
  }

  ++consecutiveOverloads_;
  if (consecutiveOverloads_ >= 3) {
    effectsBypassed_ = true;
  }
}

void RuntimeState::reportStableCallback()
{
  consecutiveOverloads_ = 0;
}

void RuntimeState::observeRealtimeStats(uint64_t previousOverBudget, uint64_t currentOverBudget)
{
  if (currentOverBudget > previousOverBudget) {
    reportOverload();
  } else {
    reportStableCallback();
  }
}

void RuntimeState::clearEffectsBypass()
{
  effectsBypassed_ = false;
  consecutiveOverloads_ = 0;
}

void RuntimeState::changePreset()
{
  effectsBypassed_ = false;
  consecutiveOverloads_ = 0;
}

bool RuntimeState::effectsBypassed() const
{
  return effectsBypassed_;
}

} // namespace ardor
