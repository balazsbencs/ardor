#pragma once

namespace ardor {

class RuntimeState {
public:
  void reportOverload();
  void reportStableCallback();
  void clearEffectsBypass();
  void changePreset();
  bool effectsBypassed() const;

private:
  bool effectsBypassed_ = false;
  int consecutiveOverloads_ = 0;
};

} // namespace ardor
