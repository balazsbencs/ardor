#pragma once

#include <cstdint>

namespace ardor {

class RuntimeState {
public:
  void reportOverload();
  void reportStableCallback();
  void observeRealtimeStats(uint64_t previousOverBudget, uint64_t currentOverBudget);
  void clearEffectsBypass();
  void changePreset();
  bool effectsBypassed() const;

private:
  bool effectsBypassed_ = false;
  int consecutiveOverloads_ = 0;
};

} // namespace ardor
