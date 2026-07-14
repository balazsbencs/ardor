#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioBackend.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace ardor {

struct MiniaudioBackendState {
  ma_context context{};
  ma_device device{};
  std::atomic<PedalEngine*> engine{nullptr};
  std::atomic<PedalEngine*> pendingEngine{nullptr};
  std::atomic<bool> swapRequested{false};
  std::atomic<bool> swapCompleted{false};
  ma_uint32 captureChannels = 1;
  ma_uint32 inputChannel = 0;
  OutputChannel outputChannel = OutputChannel::Both;
  bool contextReady = false;
  bool deviceReady = false;
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> xruns{0};
  std::atomic<uint64_t> totalCallbackNs{0};
  std::atomic<uint64_t> maxCallbackNs{0};
  std::atomic<uint32_t> callbacksInFlight{0};
  double budgetMs = 0.0;
  double sampleRate = 48000.0;
  ma_uint32 blockSize = 64;
  ma_uint32 inputFill = 0;
  ma_uint32 outputRead = 0;
  ma_uint32 outputAvailable = 0;
  // Written only by the audio callback after construction.
  enum class TransitionPhase { Normal, FadeOut, FadeIn } transitionPhase = TransitionPhase::Normal;
  ma_uint32 transitionFrame = 0;
  ma_uint32 transitionFrames = 480; // 10 ms at the fixed 48 kHz engine rate.
  std::vector<float> inputBlock;
  std::vector<float> leftBlock;
  std::vector<float> rightBlock;
};

namespace {

void callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
  auto* state = static_cast<MiniaudioBackendState*>(device->pUserData);
  auto* in = static_cast<const float*>(input);
  auto* out = static_cast<float*>(output);

  if (!state) {
    std::fill(out, out + frameCount * 2, 0.0f);
    return;
  }

  state->callbacksInFlight.fetch_add(1, std::memory_order_seq_cst);
  PedalEngine* engine = state->engine.load(std::memory_order_seq_cst);
  if (!engine || !in) {
    std::fill(out, out + frameCount * 2, 0.0f);
    state->callbacksInFlight.fetch_sub(1, std::memory_order_seq_cst);
    return;
  }

  const auto start = std::chrono::steady_clock::now();

  if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::Normal
      && state->swapRequested.load(std::memory_order_acquire)) {
    state->transitionPhase = MiniaudioBackendState::TransitionPhase::FadeOut;
    state->transitionFrame = 0;
  }

  for (ma_uint32 i = 0; i < frameCount; ++i) {
    state->inputBlock[state->inputFill++] = in[i * state->captureChannels + state->inputChannel];

    float left = 0.0f;
    float right = 0.0f;
    if (state->outputRead < state->outputAvailable) {
      left = state->leftBlock[state->outputRead];
      right = state->rightBlock[state->outputRead];
      ++state->outputRead;
      if (state->outputRead == state->outputAvailable) {
        state->outputRead = 0;
        state->outputAvailable = 0;
      }
    }

    float transitionGain = 1.0f;
    if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeOut) {
      transitionGain = 1.0f - static_cast<float>(state->transitionFrame + 1)
                                 / static_cast<float>(state->transitionFrames);
    } else if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeIn) {
      transitionGain = static_cast<float>(state->transitionFrame + 1)
                       / static_cast<float>(state->transitionFrames);
    }
    left *= transitionGain;
    right *= transitionGain;
    out[i * 2] = (state->outputChannel == OutputChannel::Right) ? 0.0f : left;
    out[i * 2 + 1] = (state->outputChannel == OutputChannel::Left) ? 0.0f : right;

    if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeOut) {
      if (++state->transitionFrame == state->transitionFrames) {
        PedalEngine* replacement = state->pendingEngine.exchange(nullptr, std::memory_order_acq_rel);
        if (replacement) {
          // The old output FIFO and partial input quantum belong to the old
          // program. They are inaudible at this point, so discard them before
          // the first new-engine sample to keep the handoff block-aligned.
          state->inputFill = 0;
          state->outputRead = 0;
          state->outputAvailable = 0;
          state->engine.store(replacement, std::memory_order_release);
          engine = replacement;
        }
        state->swapRequested.store(false, std::memory_order_release);
        state->swapCompleted.store(true, std::memory_order_release);
        state->transitionPhase = MiniaudioBackendState::TransitionPhase::FadeIn;
        state->transitionFrame = 0;
      }
    } else if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeIn
               && ++state->transitionFrame == state->transitionFrames) {
      state->transitionPhase = MiniaudioBackendState::TransitionPhase::Normal;
      state->transitionFrame = 0;
    }

    if (state->inputFill == state->blockSize) {
      engine->processBlock(state->inputBlock.data(), state->leftBlock.data(), state->rightBlock.data(),
                           state->blockSize);
      state->inputFill = 0;
      state->outputRead = 0;
      state->outputAvailable = state->blockSize;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
  const double budgetSeconds = static_cast<double>(frameCount) / state->sampleRate;
  const auto elapsedNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  state->totalCallbackNs.fetch_add(elapsedNs, std::memory_order_relaxed);
  auto currentMax = state->maxCallbackNs.load(std::memory_order_relaxed);
  while (elapsedNs > currentMax
         && !state->maxCallbackNs.compare_exchange_weak(currentMax, elapsedNs, std::memory_order_relaxed)) {
  }
  if (elapsedSeconds > budgetSeconds) {
    state->xruns.fetch_add(1, std::memory_order_relaxed);
  }
  state->callbacks.fetch_add(1, std::memory_order_relaxed);
  state->callbacksInFlight.fetch_sub(1, std::memory_order_seq_cst);
}

} // namespace

MiniaudioBackend::~MiniaudioBackend()
{
  stop();
}

uint32_t captureChannelCountForInput(uint32_t inputChannel)
{
  return std::max<uint32_t>(2, inputChannel + 1);
}

bool MiniaudioBackend::start(PedalEngine& engine, const RealtimeOptions& options)
{
  if (options.sampleRate != 48000) {
    std::cerr << "Realtime audio requires a 48000 Hz sample rate.\n";
    return false;
  }
  if (options.blockSize == 0) {
    std::cerr << "Realtime block size must be greater than zero.\n";
    return false;
  }

  stop();
  state_ = new MiniaudioBackendState();
  state_->engine.store(&engine, std::memory_order_relaxed);
  state_->captureChannels = captureChannelCountForInput(options.inputChannel);
  state_->inputChannel = options.inputChannel;
  state_->outputChannel = options.outputChannel;
  state_->sampleRate = static_cast<double>(options.sampleRate);
  state_->budgetMs = static_cast<double>(options.blockSize) / static_cast<double>(options.sampleRate) * 1000.0;
  state_->blockSize = options.blockSize;
  state_->inputBlock.assign(options.blockSize, 0.0f);
  state_->leftBlock.assign(options.blockSize, 0.0f);
  state_->rightBlock.assign(options.blockSize, 0.0f);
  engine.setSampleRate(options.sampleRate);
  engine.prepareBlockSize(options.blockSize);

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

bool MiniaudioBackend::replaceEngine(PedalEngine& engine)
{
  if (!state_) {
    return false;
  }

  if (state_->swapRequested.load(std::memory_order_acquire)) {
    return false;
  }
  state_->swapCompleted.store(false, std::memory_order_release);
  state_->pendingEngine.store(&engine, std::memory_order_release);
  state_->swapRequested.store(true, std::memory_order_release);
  while (!state_->swapCompleted.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
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

RealtimeStats MiniaudioBackend::stats() const
{
  RealtimeStats out;
  if (!state_) return out;

  out.callbacks = state_->callbacks.load(std::memory_order_relaxed);
  out.overBudget = state_->xruns.load(std::memory_order_relaxed);
  out.maxMs = static_cast<double>(state_->maxCallbackNs.load(std::memory_order_relaxed)) / 1000000.0;
  out.budgetMs = state_->budgetMs;
  if (out.callbacks > 0) {
    out.averageMs = static_cast<double>(state_->totalCallbackNs.load(std::memory_order_relaxed)) / 1000000.0
                    / static_cast<double>(out.callbacks);
  }
  return out;
}

} // namespace ardor
