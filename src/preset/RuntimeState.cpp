#include "preset/RuntimeState.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace ardor {

RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, uint64_t callbackGaps,
                                      double maxMs, double averageMs, double budgetMs, bool bypassed,
                                      uint64_t parallelWaitOverBudget,
                                      uint64_t nonFiniteBlocks,
                                      uint64_t blockSizeMismatches,
                                      double recentAverageMs)
{
  RuntimeTelemetry telemetry;
  telemetry.callbacks = callbacks;
  telemetry.overBudget = overBudget;
  telemetry.overBudgetPercent = callbacks == 0 ? 0.0 : static_cast<double>(overBudget) * 100.0 / static_cast<double>(callbacks);
  telemetry.callbackGaps = callbackGaps;
  telemetry.maxMs = maxMs;
  telemetry.averageMs = averageMs;
  telemetry.recentAverageMs = recentAverageMs >= 0.0 ? recentAverageMs : averageMs;
  telemetry.budgetMs = budgetMs;
  telemetry.bufferFreePercent =
    audioBufferFreePercent(telemetry.recentAverageMs, telemetry.budgetMs);
  telemetry.bypassed = bypassed;
  telemetry.parallelWaitOverBudget = parallelWaitOverBudget;
  telemetry.nonFiniteBlocks = nonFiniteBlocks;
  telemetry.blockSizeMismatches = blockSizeMismatches;
  return telemetry;
}

double recentCallbackAverageMs(uint64_t previousCallbacks, double previousTotalProcessingMs,
                               uint64_t currentCallbacks, double currentTotalProcessingMs,
                               double fallbackAverageMs)
{
  if (currentCallbacks <= previousCallbacks
      || currentTotalProcessingMs < previousTotalProcessingMs) {
    return std::max(0.0, fallbackAverageMs);
  }
  const auto callbackDelta = currentCallbacks - previousCallbacks;
  return (currentTotalProcessingMs - previousTotalProcessingMs)
    / static_cast<double>(callbackDelta);
}

double audioBufferFreePercent(double processingMs, double budgetMs)
{
  if (budgetMs <= 0.0) return 0.0;
  return std::clamp((1.0 - std::max(0.0, processingMs) / budgetMs) * 100.0,
                    0.0, 100.0);
}

std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(2)
      << "callbacks=" << telemetry.callbacks
      << " over=" << telemetry.overBudget
      << " over%=" << telemetry.overBudgetPercent
      << " gaps=" << telemetry.callbackGaps
      << " max=" << telemetry.maxMs << "ms"
      << " avg=" << telemetry.averageMs << "ms"
      << " recent_avg=" << telemetry.recentAverageMs << "ms"
      << " budget=" << telemetry.budgetMs << "ms"
      << " buffer_free=" << telemetry.bufferFreePercent << "%"
      << " bypassed=" << (telemetry.bypassed ? 1 : 0)
      << " worker_over=" << telemetry.parallelWaitOverBudget
      << " nonfinite=" << telemetry.nonFiniteBlocks
      << " block_mismatch=" << telemetry.blockSizeMismatches;
  return out.str();
}

bool writeRuntimeTelemetrySnapshot(const std::filesystem::path& path,
                                   const RuntimeTelemetry& telemetry,
                                   std::string& error)
{
  error.clear();
  if (path.empty()) {
    error = "telemetry path is empty";
    return false;
  }

  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not open telemetry temporary file: " + temporary.string();
      return false;
    }
    output << formatRuntimeTelemetry(telemetry) << '\n';
    output.flush();
    if (!output) {
      error = "could not write telemetry temporary file: " + temporary.string();
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    error = "could not publish telemetry snapshot: " + path.string();
    return false;
  }
  return true;
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
