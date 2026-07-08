#pragma once

#include <cstdint>

namespace ardor {

class RuntimeState {
public:
  void observeRealtimeStats(uint64_t previousCallbacks,
                            uint64_t currentCallbacks,
                            uint64_t previousOverBudget,
                            uint64_t currentOverBudget);
  void clearEffectsBypass();
  void changePreset();
  bool effectsBypassed() const;

private:
  bool effectsBypassed_ = false;
  int consecutiveBadSeconds_ = 0;
  int consecutiveStableSeconds_ = 0;
};

} // namespace ardor
