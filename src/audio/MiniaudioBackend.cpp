#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioBackend.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <pthread.h>
#if defined(__linux__)
#include <sched.h>
#endif
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
  std::atomic<bool> deviceStopped{false};
  std::atomic<bool> schedulerCaptureStarted{false};
  std::atomic<bool> schedulerCaptured{false};
  std::atomic<int> schedulerPolicy{-1};
  std::atomic<int> schedulerPriority{-1};
  std::atomic<int> schedulerSetupError{0};
  ma_uint32 captureChannels = 1;
  ma_uint32 inputChannel = 0;
  OutputChannel outputChannel = OutputChannel::Both;
  bool contextReady = false;
  bool deviceReady = false;
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> overBudgetCallbacks{0};
  std::atomic<uint64_t> callbackGapCount{0};
  std::atomic<uint64_t> lastCallbackStartNs{0};
  std::atomic<uint64_t> totalCallbackNs{0};
  std::atomic<uint64_t> maxCallbackNs{0};
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

constexpr int kArdorRealtimePriority = 70;

const char* schedulerPolicyName(int policy)
{
#if defined(__linux__)
  if (policy == SCHED_FIFO) return "SCHED_FIFO";
  if (policy == SCHED_RR) return "SCHED_RR";
  if (policy == SCHED_OTHER) return "SCHED_OTHER";
#else
  (void)policy;
#endif
  return "unknown";
}

void captureCallbackScheduler(MiniaudioBackendState& state)
{
  bool expected = false;
  if (!state.schedulerCaptureStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return;
  }

#if defined(__linux__)
  sched_param requested{};
  requested.sched_priority = kArdorRealtimePriority;
  state.schedulerSetupError.store(pthread_setschedparam(pthread_self(), SCHED_FIFO, &requested),
                                  std::memory_order_relaxed);
#endif

#if defined(__aarch64__)
  // miniaudio enables FTZ/DAZ on x86 only. AArch64 callback threads inherit a
  // default FPCR with FZ clear, so long feedback tails can enter the costly
  // subnormal path on the Pi's Cortex-A72. This flag runs once per device;
  // the setting is thread-local and remains active for callback processing.
  uint64_t fpcr = 0;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (uint64_t{1} << 24); // FPCR.FZ
  asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif

  int policy = -1;
  sched_param actual{};
  if (pthread_getschedparam(pthread_self(), &policy, &actual) == 0) {
    state.schedulerPolicy.store(policy, std::memory_order_relaxed);
    state.schedulerPriority.store(actual.sched_priority, std::memory_order_relaxed);
  }
  state.schedulerCaptured.store(true, std::memory_order_release);
}

void recordCallbackTiming(MiniaudioBackendState& state,
                          std::chrono::steady_clock::time_point start,
                          ma_uint32 frameCount)
{
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
  const double budgetSeconds = static_cast<double>(frameCount) / state.sampleRate;
  const auto elapsedNs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  state.totalCallbackNs.fetch_add(elapsedNs, std::memory_order_relaxed);
  auto currentMax = state.maxCallbackNs.load(std::memory_order_relaxed);
  while (elapsedNs > currentMax
         && !state.maxCallbackNs.compare_exchange_weak(currentMax, elapsedNs, std::memory_order_relaxed)) {
  }
  if (elapsedSeconds > budgetSeconds) {
    state.overBudgetCallbacks.fetch_add(1, std::memory_order_relaxed);
  }

  // overBudgetCallbacks catches this callback's own processing running long;
  // it cannot see the device simply failing to wake this thread on schedule.
  // Track the interval between successive callback starts separately so a
  // scheduling stall (this thread starved, not overrunning) is attributable.
  const auto startNs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());
  const auto previousStartNs = state.lastCallbackStartNs.exchange(startNs, std::memory_order_relaxed);
  if (previousStartNs != 0) {
    const auto intervalNs = startNs - previousStartNs;
    const auto expectedNs = static_cast<uint64_t>(budgetSeconds * 1e9);
    if (intervalNs > expectedNs + expectedNs / 2) {
      state.callbackGapCount.fetch_add(1, std::memory_order_relaxed);
    }
  }

  state.callbacks.fetch_add(1, std::memory_order_relaxed);
}

void deviceNotification(const ma_device_notification* notification)
{
  if (!notification || !notification->pDevice) {
    return;
  }
  auto* state = static_cast<MiniaudioBackendState*>(notification->pDevice->pUserData);
  if (!state) {
    return;
  }

  captureCallbackScheduler(*state);
  if (notification->type == ma_device_notification_type_stopped
      || notification->type == ma_device_notification_type_interruption_began) {
    state->deviceStopped.store(true, std::memory_order_release);
  }
}

void callback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
  auto* state = static_cast<MiniaudioBackendState*>(device->pUserData);
  auto* in = static_cast<const float*>(input);
  auto* out = static_cast<float*>(output);

  if (!state) {
    std::fill(out, out + frameCount * 2, 0.0f);
    return;
  }

  PedalEngine* engine = state->engine.load(std::memory_order_seq_cst);
  if (!engine || !in) {
    std::fill(out, out + frameCount * 2, 0.0f);
    return;
  }

  const auto start = std::chrono::steady_clock::now();

  // The device normally invokes us with the prepared engine quantum. Process
  // that block before emitting it, rather than feeding the FIFO after taking
  // its output; this removes the otherwise unavoidable one-quantum latency.
  // Irregular device callbacks continue through the FIFO below.
  if (frameCount == state->blockSize
      && state->inputFill == 0
      && state->outputAvailable == 0
      && state->transitionPhase == MiniaudioBackendState::TransitionPhase::Normal
      && !state->swapRequested.load(std::memory_order_acquire)) {
    for (ma_uint32 i = 0; i < frameCount; ++i) {
      state->inputBlock[i] = in[i * state->captureChannels + state->inputChannel];
    }
    engine->processBlock(state->inputBlock.data(), state->leftBlock.data(), state->rightBlock.data(), frameCount);
    for (ma_uint32 i = 0; i < frameCount; ++i) {
      const float left = state->leftBlock[i];
      const float right = state->rightBlock[i];
      out[i * 2] = (state->outputChannel == OutputChannel::Right) ? 0.0f : left;
      out[i * 2 + 1] = (state->outputChannel == OutputChannel::Left) ? 0.0f : right;
    }
    recordCallbackTiming(*state, start, frameCount);
    return;
  }

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
  recordCallbackTiming(*state, start, frameCount);
}

bool waitForCallbackScheduler(const MiniaudioBackendState& state)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  while (!state.schedulerCaptured.load(std::memory_order_acquire)) {
    if (state.deviceStopped.load(std::memory_order_acquire)
        || std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

} // namespace

bool hasRequiredRealtimeScheduler(const RealtimeStats& stats)
{
#if defined(__linux__)
  return stats.schedulerCaptured && stats.schedulerPolicy == SCHED_FIFO
         && stats.schedulerPriority == kArdorRealtimePriority;
#else
  (void)stats;
  return false;
#endif
}

MiniaudioBackend::MiniaudioBackend() = default;

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
  state_ = std::make_unique<MiniaudioBackendState>();
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

  ma_context_config contextConfig = ma_context_config_init();
  contextConfig.threadPriority = ma_thread_priority_realtime;
  if (ma_context_init(nullptr, 0, &contextConfig, &state_->context) != MA_SUCCESS) {
    state_.reset();
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
  cfg.notificationCallback = deviceNotification;
  cfg.pUserData = state_.get();
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

  if (options.requireNativeSampleRate
      && (state_->device.capture.internalSampleRate != options.sampleRate
          || state_->device.playback.internalSampleRate != options.sampleRate)) {
    std::cerr << "Realtime audio requires native " << options.sampleRate
              << " Hz capture and playback; device conversion is not permitted in production.\n";
    stop();
    return false;
  }

  if (ma_device_start(&state_->device) != MA_SUCCESS) {
    stop();
    return false;
  }

  if (!waitForCallbackScheduler(*state_)) {
    std::cerr << "Realtime audio callback did not start within 250 ms.\n";
    stop();
    return false;
  }

  const auto realtimeStats = stats();
  std::cerr << "Callback scheduler: " << schedulerPolicyName(realtimeStats.schedulerPolicy)
            << " (" << realtimeStats.schedulerPolicy << "), priority "
            << realtimeStats.schedulerPriority;
  if (realtimeStats.schedulerSetupError != 0) {
    std::cerr << ", FIFO setup error " << realtimeStats.schedulerSetupError;
  }
  std::cerr << "\n";

#if defined(__linux__)
  if (options.requireRealtimeScheduler && !hasRequiredRealtimeScheduler(realtimeStats)) {
    std::cerr << "Realtime audio requires SCHED_FIFO/" << kArdorRealtimePriority
              << "; grant CAP_SYS_NICE or use --allow-non-realtime for development only.\n";
    stop();
    return false;
  }
#endif

  std::cerr << "Realtime started: client " << options.sampleRate << " Hz, block " << options.blockSize
            << ", input channel " << options.inputChannel
            << ", native capture " << state_->device.capture.internalSampleRate << " Hz/"
            << state_->device.capture.internalPeriodSizeInFrames
            << ", playback " << state_->device.playback.internalSampleRate << " Hz/"
            << state_->device.playback.internalPeriodSizeInFrames << "\n";
  return true;
}

EngineReplaceResult MiniaudioBackend::replaceEngine(PedalEngine& engine)
{
  if (!state_) {
    return EngineReplaceResult::DeviceStopped;
  }

  if (state_->swapRequested.load(std::memory_order_acquire)) {
    return EngineReplaceResult::Busy;
  }
  if (state_->deviceStopped.load(std::memory_order_acquire)
      || ma_device_get_state(&state_->device) != ma_device_state_started) {
    stop();
    return EngineReplaceResult::DeviceStopped;
  }
  state_->swapCompleted.store(false, std::memory_order_release);
  state_->pendingEngine.store(&engine, std::memory_order_release);
  state_->swapRequested.store(true, std::memory_order_release);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!state_->swapCompleted.load(std::memory_order_acquire)) {
    if (state_->deviceStopped.load(std::memory_order_acquire)
        || ma_device_get_state(&state_->device) != ma_device_state_started) {
      stop();
      return EngineReplaceResult::DeviceStopped;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      stop();
      return EngineReplaceResult::TimedOut;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return EngineReplaceResult::Activated;
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
  state_.reset();
}

uint64_t MiniaudioBackend::callbackCount() const
{
  return state_ ? state_->callbacks.load(std::memory_order_relaxed) : 0;
}

uint64_t MiniaudioBackend::overBudgetCallbackCount() const
{
  return state_ ? state_->overBudgetCallbacks.load(std::memory_order_relaxed) : 0;
}

uint64_t MiniaudioBackend::callbackGapCount() const
{
  return state_ ? state_->callbackGapCount.load(std::memory_order_relaxed) : 0;
}

bool MiniaudioBackend::deviceStopped() const
{
  return !state_ || state_->deviceStopped.load(std::memory_order_acquire);
}

RealtimeStats MiniaudioBackend::stats() const
{
  RealtimeStats out;
  if (!state_) return out;

  out.callbacks = state_->callbacks.load(std::memory_order_relaxed);
  out.overBudget = state_->overBudgetCallbacks.load(std::memory_order_relaxed);
  out.callbackGaps = state_->callbackGapCount.load(std::memory_order_relaxed);
  out.maxMs = static_cast<double>(state_->maxCallbackNs.load(std::memory_order_relaxed)) / 1000000.0;
  out.budgetMs = state_->budgetMs;
  out.schedulerCaptured = state_->schedulerCaptured.load(std::memory_order_acquire);
  out.schedulerPolicy = state_->schedulerPolicy.load(std::memory_order_relaxed);
  out.schedulerPriority = state_->schedulerPriority.load(std::memory_order_relaxed);
  out.schedulerSetupError = state_->schedulerSetupError.load(std::memory_order_relaxed);
  if (out.callbacks > 0) {
    out.averageMs = static_cast<double>(state_->totalCallbackNs.load(std::memory_order_relaxed)) / 1000000.0
                    / static_cast<double>(out.callbacks);
  }
  return out;
}

} // namespace ardor
