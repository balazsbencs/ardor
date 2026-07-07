#include "preset/RuntimeState.h"

namespace ardor {

void RuntimeState::reportOverload()
{
  effectsBypassed_ = true;
}

void RuntimeState::reportStableCallback()
{
}

void RuntimeState::clearEffectsBypass()
{
  effectsBypassed_ = false;
}

void RuntimeState::changePreset()
{
  effectsBypassed_ = false;
}

bool RuntimeState::effectsBypassed() const
{
  return effectsBypassed_;
}

} // namespace ardor
