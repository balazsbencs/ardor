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

class MiniaudioBackend {
public:
  ~MiniaudioBackend();

  bool start(PedalEngine& engine, const RealtimeOptions& options);
  void stop();
  uint64_t callbackCount() const;
  uint64_t xrunCount() const;

private:
  MiniaudioBackendState* state_ = nullptr;
};

} // namespace ardor
