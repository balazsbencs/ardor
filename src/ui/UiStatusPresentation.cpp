#include "ui/UiStatusPresentation.h"

#include <cstdio>
#include <algorithm>

namespace ardor {
namespace {

constexpr std::uint32_t kMuted = 0xa6a6a6;
constexpr std::uint32_t kWarning = 0xffb347;
constexpr std::uint32_t kDanger = 0xf97373;

} // namespace

UiTelemetryPresentation makeTelemetryPresentation(const UiState& state,
                                                  std::uint32_t accentColor)
{
  char text[96]{};
  if (!state.clipDebug.enabled) {
    if (state.telemetry.budgetMs <= 0.0) {
      std::snprintf(text, sizeof(text), "BUFFER --%% USED");
    } else {
      const auto used = std::clamp(100.0 - state.telemetry.bufferFreePercent, 0.0, 100.0);
      std::snprintf(text, sizeof(text), "BUFFER %.0f%% USED", used);
    }
  } else if (state.clipDebug.overloaded) {
    std::snprintf(text, sizeof(text), "CLIP  %s  %+.1fdB  %lluf",
                  state.clipDebug.firstStage.c_str(), state.clipDebug.peakDb,
                  static_cast<unsigned long long>(state.clipDebug.overloadFrames));
  } else if (state.clipDebug.limiterFrames > 0) {
    std::snprintf(text, sizeof(text), "LIMIT  %+.1fdB  %lluf",
                  state.clipDebug.peakDb,
                  static_cast<unsigned long long>(state.clipDebug.limiterFrames));
  } else {
    std::snprintf(text, sizeof(text), "LEVEL OK  %+.1fdB", state.clipDebug.peakDb);
  }

  std::uint32_t color = accentColor;
  if (state.effectsBypassed || (state.clipDebug.enabled && state.clipDebug.overloaded)) {
    color = kDanger;
  } else if (state.clipDebug.enabled && state.clipDebug.limiterFrames > 0) {
    color = kWarning;
  } else if (!state.clipDebug.enabled && state.telemetry.budgetMs <= 0.0) {
    color = kMuted;
  } else if (!state.clipDebug.enabled && state.telemetry.bufferFreePercent < 15.0) {
    color = kDanger;
  } else if (!state.clipDebug.enabled && state.telemetry.bufferFreePercent < 30.0) {
    color = kWarning;
  }
  return {text, color};
}

} // namespace ardor
