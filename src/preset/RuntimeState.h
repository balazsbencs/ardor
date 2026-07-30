#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ardor {

struct RuntimeTelemetry {
  uint64_t callbacks = 0;
  uint64_t overBudget = 0;
  double overBudgetPercent = 0.0;
  uint64_t callbackGaps = 0;
  double maxMs = 0.0;
  double averageMs = 0.0;
  double recentAverageMs = 0.0;
  double budgetMs = 0.0;
  double bufferFreePercent = 0.0;
  bool bypassed = false;
  uint64_t parallelWaitOverBudget = 0;
  uint64_t nonFiniteBlocks = 0;
  uint64_t blockSizeMismatches = 0;
};

RuntimeTelemetry makeRuntimeTelemetry(uint64_t callbacks, uint64_t overBudget, uint64_t callbackGaps,
                                      double maxMs, double averageMs, double budgetMs, bool bypassed,
                                      uint64_t parallelWaitOverBudget = 0,
                                      uint64_t nonFiniteBlocks = 0,
                                      uint64_t blockSizeMismatches = 0,
                                      double recentAverageMs = -1.0);
double recentCallbackAverageMs(uint64_t previousCallbacks, double previousTotalProcessingMs,
                               uint64_t currentCallbacks, double currentTotalProcessingMs,
                               double fallbackAverageMs);
double audioBufferFreePercent(double processingMs, double budgetMs);
std::string formatRuntimeTelemetry(const RuntimeTelemetry& telemetry);
bool writeRuntimeTelemetrySnapshot(const std::filesystem::path& path,
                                   const RuntimeTelemetry& telemetry,
                                   std::string& error);

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
