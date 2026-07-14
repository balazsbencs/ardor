#include "preset/RuntimeState.h"

#include <iomanip>
#include <sstream>

namespace ardor {

RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, double maxMs,
                                      double averageMs, double budgetMs, bool bypassed)
{
  RuntimeTelemetry telemetry;
  telemetry.callbacks = callbacks;
  telemetry.overBudget = overBudget;
  telemetry.overBudgetPercent = callbacks == 0 ? 0.0 : static_cast<double>(overBudget) * 100.0 / static_cast<double>(callbacks);
  telemetry.maxMs = maxMs;
  telemetry.averageMs = averageMs;
  telemetry.budgetMs = budgetMs;
  telemetry.bypassed = bypassed;
  return telemetry;
}

std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(2)
      << "callbacks=" << telemetry.callbacks
      << " over=" << telemetry.overBudget
      << " over%=" << telemetry.overBudgetPercent
      << " max=" << telemetry.maxMs << "ms"
      << " avg=" << telemetry.averageMs << "ms"
      << " budget=" << telemetry.budgetMs << "ms"
      << " bypassed=" << (telemetry.bypassed ? 1 : 0);
  return out.str();
}

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
    if (consecutiveBadSeconds_ >= 3) {
      effectsBypassed_ = true;
    }
  } else {
    consecutiveBadSeconds_ = 0;
    // An overload bypass is intentionally latched. Re-enabling expensive DSP
    // every few seconds can cause a repeating xrun/bypass cycle; recovery is
    // an explicit user or preset action through clearEffectsBypass/changePreset.
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
