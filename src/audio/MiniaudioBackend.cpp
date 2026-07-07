#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioBackend.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <iostream>

namespace ardor {

struct MiniaudioBackendState {
  ma_device device{};
  PedalEngine* engine = nullptr;
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> xruns{0};
};

namespace {

void callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
  auto* state = static_cast<MiniaudioBackendState*>(device->pUserData);
  auto* in = static_cast<const float*>(input);
  auto* out = static_cast<float*>(output);

  if (!state || !state->engine || !in) {
    std::fill(out, out + frameCount * 2, 0.0f);
    return;
  }

  for (ma_uint32 i = 0; i < frameCount; ++i) {
    const auto [left, right] = state->engine->process(in[i]);
    out[i * 2] = left;
    out[i * 2 + 1] = right;
  }
  state->callbacks.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

MiniaudioBackend::~MiniaudioBackend()
{
  stop();
}

bool MiniaudioBackend::start(PedalEngine& engine, uint32_t sampleRate, uint32_t blockSize)
{
  stop();
  state_ = new MiniaudioBackendState();
  state_->engine = &engine;

  ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 1;
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = sampleRate;
  cfg.periodSizeInFrames = blockSize;
  cfg.dataCallback = callback;
  cfg.pUserData = state_;

  if (ma_device_init(nullptr, &cfg, &state_->device) != MA_SUCCESS) {
    delete state_;
    state_ = nullptr;
    return false;
  }

  if (ma_device_start(&state_->device) != MA_SUCCESS) {
    stop();
    return false;
  }

  std::cerr << "Realtime started: " << sampleRate << " Hz, block " << blockSize << "\n";
  return true;
}

void MiniaudioBackend::stop()
{
  if (!state_) return;
  ma_device_uninit(&state_->device);
  delete state_;
  state_ = nullptr;
}

uint64_t MiniaudioBackend::callbackCount() const
{
  return state_ ? state_->callbacks.load(std::memory_order_relaxed) : 0;
}

uint64_t MiniaudioBackend::xrunCount() const
{
  return state_ ? state_->xruns.load(std::memory_order_relaxed) : 0;
}

} // namespace ardor
