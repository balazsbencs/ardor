#pragma once

#include "dsp/PedalEngine.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ardor {

struct MiniaudioBackendState;

enum class OutputChannel {
  Both,
  Left,
  Right
};

struct RealtimeOptions {
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  int captureDeviceIndex = -1;
  int playbackDeviceIndex = -1;
  uint32_t inputChannel = 0;
  OutputChannel outputChannel = OutputChannel::Both;
  // Linux production runs require the callback to report SCHED_FIFO/70.
  // Development callers may explicitly opt out when they lack CAP_SYS_NICE.
  bool requireRealtimeScheduler = false;
  // The Pi product must not hide converter latency behind a requested client
  // rate. Development callers may explicitly permit a converted device.
  bool requireNativeSampleRate = false;
};

struct RealtimeStats {
  uint64_t callbacks = 0;
  uint64_t overBudget = 0;
  // Distinct from overBudget: a gap between successive callback starts wider
  // than 1.5x the expected quantum period means the device failed to wake
  // this thread on schedule, not that this callback's own work ran long.
  uint64_t callbackGaps = 0;
  double averageMs = 0.0;
  double maxMs = 0.0;
  double budgetMs = 0.0;
  bool schedulerCaptured = false;
  int schedulerPolicy = -1;
  int schedulerPriority = -1;
  int schedulerSetupError = 0;
};

enum class EngineReplaceResult {
  Activated,
  Busy,
  DeviceStopped,
  TimedOut,
};

uint32_t captureChannelCountForInput(uint32_t inputChannel);
bool hasRequiredRealtimeScheduler(const RealtimeStats& stats);

class MiniaudioBackend {
public:
  MiniaudioBackend();
  ~MiniaudioBackend();

  bool start(PedalEngine& engine, const RealtimeOptions& options);
  // Fades the active program out, adopts a preconfigured engine at silence,
  // then fades in without stopping the device. The caller keeps the previous
  // engine alive until this returns.
  // This is a control-thread-only operation. A stopped callback cannot retain
  // the supplied engine: on device loss or timeout the backend is quiesced
  // before this method returns.
  EngineReplaceResult replaceEngine(PedalEngine& engine);
  void stop();
  uint64_t callbackCount() const;
  uint64_t overBudgetCallbackCount() const;
  uint64_t callbackGapCount() const;
  bool deviceStopped() const;
  RealtimeStats stats() const;
  // Host-level mute is applied after DSP while capture remains available to
  // the tuner. The callback ramps it over 10 ms to avoid a click.
  void setOutputMuted(bool muted);
  // Copies raw mono capture samples from the callback's SPSC monitor ring.
  std::size_t readCapturedInput(float* output, std::size_t capacity);
  void discardCapturedInput();

private:
  std::unique_ptr<MiniaudioBackendState> state_;
  bool desiredOutputMuted_ = false; // control thread only; survives device recovery
};

} // namespace ardor
