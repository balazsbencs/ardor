#include "control/ControlEvents.h"

#include <algorithm>

namespace ardor {

bool applyControlEvent(ControlState& state, const ControlEvent& event)
{
  if (event.type == ControlEventType::FootswitchPressed) {
    if (event.index < 0 || event.index >= 4) {
      return false;
    }
    state.activeSlot = event.index;
    return true;
  }

  state.masterVolume = std::clamp(state.masterVolume + event.delta, 0, 100);
  return true;
}

} // namespace ardor
