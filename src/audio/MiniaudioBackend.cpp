#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioBackend.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>

namespace ardor {

struct MiniaudioBackendState {
  ma_context context{};
  ma_device device{};
  PedalEngine* engine = nullptr;
  ma_uint32 captureChannels = 1;
  ma_uint32 inputChannel = 0;
  OutputChannel outputChannel = OutputChannel::Both;
  bool contextReady = false;
  bool deviceReady = false;
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> xruns{0};
  double sampleRate = 48000.0;
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

  const auto start = std::chrono::steady_clock::now();
  for (ma_uint32 i = 0; i < frameCount; ++i) {
    const float sample = in[i * state->captureChannels + state->inputChannel];
    const auto [left, right] = state->engine->process(sample);
    out[i * 2] = (state->outputChannel == OutputChannel::Right) ? 0.0f : left;
    out[i * 2 + 1] = (state->outputChannel == OutputChannel::Left) ? 0.0f : right;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
  const double budgetSeconds = static_cast<double>(frameCount) / state->sampleRate;
  if (elapsedSeconds > budgetSeconds) {
    state->xruns.fetch_add(1, std::memory_order_relaxed);
  }
  state->callbacks.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

MiniaudioBackend::~MiniaudioBackend()
{
  stop();
}

bool MiniaudioBackend::start(PedalEngine& engine, const RealtimeOptions& options)
{
  stop();
  state_ = new MiniaudioBackendState();
  state_->engine = &engine;
  state_->captureChannels = options.inputChannel + 1;
  state_->inputChannel = options.inputChannel;
  state_->outputChannel = options.outputChannel;
  state_->sampleRate = static_cast<double>(options.sampleRate);

  if (ma_context_init(nullptr, 0, nullptr, &state_->context) != MA_SUCCESS) {
    delete state_;
    state_ = nullptr;
    return false;
  }
  state_->contextReady = true;

  ma_device_info* playback = nullptr;
  ma_uint32 playbackCount = 0;
  ma_device_info* capture = nullptr;
  ma_uint32 captureCount = 0;
  if (ma_context_get_devices(&state_->context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
    stop();
    return false;
  }

  if (options.captureDeviceIndex < -1 || options.playbackDeviceIndex < -1
      || options.captureDeviceIndex >= static_cast<int>(captureCount)
      || options.playbackDeviceIndex >= static_cast<int>(playbackCount)) {
    std::cerr << "Invalid audio device index. Run --devices.\n";
    stop();
    return false;
  }

  ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = state_->captureChannels;
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = options.sampleRate;
  cfg.periodSizeInFrames = options.blockSize;
  cfg.dataCallback = callback;
  cfg.pUserData = state_;
  if (options.captureDeviceIndex >= 0) {
    cfg.capture.pDeviceID = &capture[options.captureDeviceIndex].id;
  }
  if (options.playbackDeviceIndex >= 0) {
    cfg.playback.pDeviceID = &playback[options.playbackDeviceIndex].id;
  }

  if (ma_device_init(&state_->context, &cfg, &state_->device) != MA_SUCCESS) {
    stop();
    return false;
  }
  state_->deviceReady = true;

  if (ma_device_start(&state_->device) != MA_SUCCESS) {
    stop();
    return false;
  }

  std::cerr << "Realtime started: " << options.sampleRate << " Hz, block " << options.blockSize
            << ", input channel " << options.inputChannel << "\n";
  return true;
}

void MiniaudioBackend::stop()
{
  if (!state_) return;
  if (state_->deviceReady) {
    ma_device_uninit(&state_->device);
  }
  if (state_->contextReady) {
    ma_context_uninit(&state_->context);
  }
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
