#pragma once

#include "dsp/PedalEngine.h"

#include <cstdint>

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
};

struct RealtimeStats {
  uint64_t callbacks = 0;
  uint64_t overBudget = 0;
  double averageMs = 0.0;
  double maxMs = 0.0;
  double budgetMs = 0.0;
};

uint32_t captureChannelCountForInput(uint32_t inputChannel);

class MiniaudioBackend {
public:
  ~MiniaudioBackend();

  bool start(PedalEngine& engine, const RealtimeOptions& options);
  // Fades the active program out, adopts a preconfigured engine at silence,
  // then fades in without stopping the device. The caller keeps the previous
  // engine alive until this returns.
  bool replaceEngine(PedalEngine& engine);
  void stop();
  uint64_t callbackCount() const;
  uint64_t xrunCount() const;
  RealtimeStats stats() const;

private:
  MiniaudioBackendState* state_ = nullptr;
};

} // namespace ardor
