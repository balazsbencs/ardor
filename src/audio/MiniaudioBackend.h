#pragma once

#include "dsp/PedalEngine.h"

#include <cstdint>

namespace ardor {

struct MiniaudioBackendState;

class MiniaudioBackend {
public:
  ~MiniaudioBackend();

  bool start(PedalEngine& engine, uint32_t sampleRate, uint32_t blockSize);
  void stop();
  uint64_t callbackCount() const;
  uint64_t xrunCount() const;

private:
  MiniaudioBackendState* state_ = nullptr;
};

} // namespace ardor
