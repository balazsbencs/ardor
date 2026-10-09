#define MINIAUDIO_IMPLEMENTATION
#include "MiniaudioBackend.h"

#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <iostream>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#if defined(__linux__)
#include <sched.h>
#endif
#include <thread>
#include <vector>

namespace ardor {

struct MiniaudioBackendState {
  static constexpr std::size_t captureMonitorCapacity = 32768;
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
  std::atomic<int> affinitySetupError{0};
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
  int audioCpu = -1;
  ma_uint32 inputFill = 0;
  ma_uint32 outputRead = 0;
  ma_uint32 outputAvailable = 0;
  // Written only by the audio callback after construction.
  enum class TransitionPhase { Normal, FadeOut, FadeIn } transitionPhase = TransitionPhase::Normal;
  ma_uint32 transitionFrame = 0;
  ma_uint32 transitionFrames = 512; // start() rounds the nominal 10 ms to whole blocks.
  std::vector<float> inputBlock;
  std::vector<float> leftBlock;
  std::vector<float> rightBlock;
  std::array<std::atomic<uint32_t>, captureMonitorCapacity> captureMonitor{};
  std::atomic<uint64_t> captureWriteCounter{0};
  uint64_t captureReadCounter = 0; // control thread only
  std::atomic<bool> outputMuted{false};
  float currentMuteGain = 1.0f; // audio callback only
};

namespace {

constexpr int kArdorRealtimePriority = 70;

void monitorInput(MiniaudioBackendState& state, float sample)
{
  const uint64_t counter = state.captureWriteCounter.load(std::memory_order_relaxed);
  state.captureMonitor[counter % MiniaudioBackendState::captureMonitorCapacity].store(
    std::bit_cast<uint32_t>(sample), std::memory_order_relaxed);
  state.captureWriteCounter.store(counter + 1, std::memory_order_release);
}

float nextMuteGain(MiniaudioBackendState& state)
{
  const float target = state.outputMuted.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  constexpr float step = 1.0f / 480.0f; // 10 ms at the fixed 48 kHz rate.
  if (state.currentMuteGain < target) {
    state.currentMuteGain = std::min(target, state.currentMuteGain + step);
  } else if (state.currentMuteGain > target) {
    state.currentMuteGain = std::max(target, state.currentMuteGain - step);
  }
  return state.currentMuteGain;
}

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
  if (state.audioCpu >= 0) {
    if (state.audioCpu >= CPU_SETSIZE) {
      state.affinitySetupError.store(EINVAL, std::memory_order_relaxed);
    } else {
      cpu_set_t cpus;
      CPU_ZERO(&cpus);
      CPU_SET(state.audioCpu, &cpus);
      state.affinitySetupError.store(
        pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus),
        std::memory_order_relaxed);
    }
  }
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
#if !defined(_WIN32)
  sched_param actual{};
  if (pthread_getschedparam(pthread_self(), &policy, &actual) == 0) {
    state.schedulerPolicy.store(policy, std::memory_order_relaxed);
    state.schedulerPriority.store(actual.sched_priority, std::memory_order_relaxed);
  }
#else
  (void)policy;
#endif
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

  if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::Normal
      && state->swapRequested.load(std::memory_order_acquire)) {
    state->transitionPhase = MiniaudioBackendState::TransitionPhase::FadeOut;
    state->transitionFrame = 0;
  }

  // The device normally invokes us with the prepared engine quantum. Process
  // that block before emitting it, rather than feeding the FIFO after taking
  // its output; this removes the otherwise unavoidable one-quantum latency.
  // Preset fades stay on this path too. transitionFrames is rounded to a whole
  // number of engine blocks, so the old engine reaches silence exactly at a
  // callback boundary and the new engine can start the following block.
  // Irregular device callbacks continue through the FIFO below.
  if (frameCount == state->blockSize
      && state->inputFill == 0
      && state->outputAvailable == 0) {
    for (ma_uint32 i = 0; i < frameCount; ++i) {
      state->inputBlock[i] = in[i * state->captureChannels + state->inputChannel];
      monitorInput(*state, state->inputBlock[i]);
    }
    engine->processBlock(state->inputBlock.data(), state->leftBlock.data(), state->rightBlock.data(), frameCount);
    for (ma_uint32 i = 0; i < frameCount; ++i) {
      float transitionGain = 1.0f;
      if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeOut) {
        transitionGain = 1.0f - static_cast<float>(state->transitionFrame + 1)
                                   / static_cast<float>(state->transitionFrames);
      } else if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeIn) {
        transitionGain = static_cast<float>(state->transitionFrame + 1)
                         / static_cast<float>(state->transitionFrames);
      }
      const float muteGain = nextMuteGain(*state);
      const float left = state->leftBlock[i] * transitionGain * muteGain;
      const float right = state->rightBlock[i] * transitionGain * muteGain;
      out[i * 2] = (state->outputChannel == OutputChannel::Right) ? 0.0f : left;
      out[i * 2 + 1] = (state->outputChannel == OutputChannel::Left) ? 0.0f : right;

      if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeOut
          && ++state->transitionFrame == state->transitionFrames) {
        PedalEngine* replacement = state->pendingEngine.exchange(nullptr, std::memory_order_acq_rel);
        if (replacement) {
          state->engine.store(replacement, std::memory_order_release);
          engine = replacement;
        }
        state->swapRequested.store(false, std::memory_order_release);
        state->swapCompleted.store(true, std::memory_order_release);
        state->transitionPhase = MiniaudioBackendState::TransitionPhase::FadeIn;
        state->transitionFrame = 0;
      } else if (state->transitionPhase == MiniaudioBackendState::TransitionPhase::FadeIn
                 && ++state->transitionFrame == state->transitionFrames) {
        state->transitionPhase = MiniaudioBackendState::TransitionPhase::Normal;
        state->transitionFrame = 0;
      }
    }
    recordCallbackTiming(*state, start, frameCount);
    return;
  }

  for (ma_uint32 i = 0; i < frameCount; ++i) {
    const float captured = in[i * state->captureChannels + state->inputChannel];
    monitorInput(*state, captured);
    state->inputBlock[state->inputFill++] = captured;

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
    const float muteGain = nextMuteGain(*state);
    left *= transitionGain * muteGain;
    right *= transitionGain * muteGain;
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

namespace {

std::string deviceIdentifier(ma_backend backend, const ma_device_info& device)
{
  switch (backend) {
    case ma_backend_coreaudio: return std::string("coreaudio:") + device.id.coreaudio;
    case ma_backend_alsa: return std::string("alsa:") + device.id.alsa;
    case ma_backend_pulseaudio: return std::string("pulse:") + device.id.pulse;
    case ma_backend_wasapi: {
      // Encode UTF-16 code units, not the union's padding or locale-dependent
      // narrow characters. The endpoint identity survives list reordering.
      std::string id = "wasapi:";
      constexpr char hex[] = "0123456789abcdef";
      for (const auto unit : device.id.wasapi) {
        if (!unit) break;
        for (int shift = 12; shift >= 0; shift -= 4) id += hex[(unit >> shift) & 15];
      }
      return id;
    }
    default: return std::to_string(static_cast<int>(backend)) + ":" + device.name;
  }
}

int findDevice(ma_backend backend, const ma_device_info* devices, ma_uint32 count,
               const std::string& identifier)
{
  int found = -1;
  for (ma_uint32 index = 0; index < count; ++index) {
    if (deviceIdentifier(backend, devices[index]) != identifier) continue;
    if (found >= 0) return -1; // Ambiguous fallback identities must not select arbitrarily.
    found = static_cast<int>(index);
  }
  return found;
}

} // namespace

bool enumerateAudioDevices(AudioDeviceList& devices, std::string& error)
{
  devices = {};
  error.clear();
  ma_context context{};
  const auto initialized = ma_context_init(nullptr, 0, nullptr, &context);
  if (initialized != MA_SUCCESS) {
    error = std::string("Cannot initialize audio: ") + ma_result_description(initialized);
    return false;
  }
  ma_device_info* playback = nullptr;
  ma_device_info* capture = nullptr;
  ma_uint32 playbackCount = 0, captureCount = 0;
  const auto enumerated = ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount);
  if (enumerated == MA_SUCCESS) {
    for (ma_uint32 index = 0; index < captureCount; ++index) {
      devices.capture.push_back({deviceIdentifier(context.backend, capture[index]), capture[index].name});
    }
    for (ma_uint32 index = 0; index < playbackCount; ++index) {
      devices.playback.push_back({deviceIdentifier(context.backend, playback[index]), playback[index].name});
    }
  } else {
    error = std::string("Cannot list audio interfaces: ") + ma_result_description(enumerated);
  }
  ma_context_uninit(&context);
  return enumerated == MA_SUCCESS;
}

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
  lastError_.clear();
  if (options.sampleRate != 48000) {
    lastError_ = "Ardor requires 48 kHz audio.";
    std::cerr << "Realtime audio requires a 48000 Hz sample rate.\n";
    return false;
  }
  if (options.blockSize == 0) {
    lastError_ = "The audio buffer size must be greater than zero.";
    std::cerr << "Realtime block size must be greater than zero.\n";
    return false;
  }

  stop();
  state_ = std::make_unique<MiniaudioBackendState>();
  state_->outputMuted.store(desiredOutputMuted_, std::memory_order_relaxed);
  state_->currentMuteGain = desiredOutputMuted_ ? 0.0f : 1.0f;
  state_->engine.store(&engine, std::memory_order_relaxed);
  state_->captureChannels = captureChannelCountForInput(options.inputChannel);
  state_->inputChannel = options.inputChannel;
  state_->outputChannel = options.outputChannel;
  state_->sampleRate = static_cast<double>(options.sampleRate);
  state_->budgetMs = static_cast<double>(options.blockSize) / static_cast<double>(options.sampleRate) * 1000.0;
  state_->blockSize = options.blockSize;
  constexpr ma_uint32 kNominalTransitionFrames = 480;
  state_->transitionFrames = std::max(
      options.blockSize,
      ((kNominalTransitionFrames + options.blockSize - 1) / options.blockSize) * options.blockSize);
  state_->audioCpu = options.audioCpu;
  state_->inputBlock.assign(options.blockSize, 0.0f);
  state_->leftBlock.assign(options.blockSize, 0.0f);
  state_->rightBlock.assign(options.blockSize, 0.0f);
  engine.setSampleRate(options.sampleRate);
  engine.prepareBlockSize(options.blockSize);

  ma_context_config contextConfig = ma_context_config_init();
  contextConfig.threadPriority = ma_thread_priority_realtime;
  if (ma_context_init(nullptr, 0, &contextConfig, &state_->context) != MA_SUCCESS) {
    lastError_ = "Cannot initialize audio. Check that your interface is connected.";
    state_.reset();
    return false;
  }
  state_->contextReady = true;

  ma_device_info* playback = nullptr;
  ma_uint32 playbackCount = 0;
  ma_device_info* capture = nullptr;
  ma_uint32 captureCount = 0;
  if (ma_context_get_devices(&state_->context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
    lastError_ = "Cannot list audio interfaces. Reconnect the interface and try again.";
    stop();
    return false;
  }

  const int captureIndex = options.captureDeviceId.empty() ? options.captureDeviceIndex
    : findDevice(state_->context.backend, capture, captureCount, options.captureDeviceId);
  const int playbackIndex = options.playbackDeviceId.empty() ? options.playbackDeviceIndex
    : findDevice(state_->context.backend, playback, playbackCount, options.playbackDeviceId);
  if ((!options.captureDeviceId.empty() && captureIndex < 0)
      || (!options.playbackDeviceId.empty() && playbackIndex < 0)) {
    lastError_ = "The selected interface is unavailable. Reconnect it or select another interface.";
    stop();
    return false;
  }
  if (captureIndex < -1 || playbackIndex < -1
      || captureIndex >= static_cast<int>(captureCount)
      || playbackIndex >= static_cast<int>(playbackCount)) {
    lastError_ = "The selected audio device is unavailable.";
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
  if (captureIndex >= 0) {
    cfg.capture.pDeviceID = &capture[captureIndex].id;
  }
  if (playbackIndex >= 0) {
    cfg.playback.pDeviceID = &playback[playbackIndex].id;
  }

  const auto initialized = ma_device_init(&state_->context, &cfg, &state_->device);
  if (initialized != MA_SUCCESS) {
    lastError_ = std::string("Cannot open the selected interface: ") + ma_result_description(initialized)
      + ". Check audio-input permission, channels, and the interface's 48 kHz setting.";
    stop();
    return false;
  }
  state_->deviceReady = true;

  if (!options.captureDeviceId.empty()
      && options.inputChannel >= state_->device.capture.internalChannels) {
    lastError_ = "This interface does not have the selected input channel. Choose another channel in Audio setup.";
    stop();
    return false;
  }

  if (options.requireNativeSampleRate
      && (state_->device.capture.internalSampleRate != options.sampleRate
          || state_->device.playback.internalSampleRate != options.sampleRate)) {
    lastError_ = "Set the selected interface to 48 kHz before starting audio.";
    std::cerr << "Realtime audio requires native " << options.sampleRate
              << " Hz capture and playback; device conversion is not permitted in production.\n";
    stop();
    return false;
  }

  const auto started = ma_device_start(&state_->device);
  if (started != MA_SUCCESS) {
    lastError_ = std::string("Cannot start audio: ") + ma_result_description(started);
    stop();
    return false;
  }

  if (!waitForCallbackScheduler(*state_)) {
    lastError_ = "The interface did not start sending audio. Reconnect it and try again.";
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
  if (options.audioCpu >= 0) {
    std::cerr << ", CPU " << options.audioCpu;
    if (realtimeStats.affinitySetupError != 0) {
      std::cerr << " affinity error " << realtimeStats.affinitySetupError;
    }
  }
  std::cerr << "\n";

#if defined(__linux__)
  if (options.requireRealtimeScheduler && !hasRequiredRealtimeScheduler(realtimeStats)) {
    lastError_ = "The required realtime scheduler is unavailable.";
    std::cerr << "Realtime audio requires SCHED_FIFO/" << kArdorRealtimePriority
              << "; grant CAP_SYS_NICE or use --allow-non-realtime for development only.\n";
    stop();
    return false;
  }
  if (options.audioCpu >= 0 && realtimeStats.affinitySetupError != 0) {
    lastError_ = "Cannot apply the requested audio CPU affinity.";
    std::cerr << "Failed to pin realtime audio callback to CPU " << options.audioCpu << ".\n";
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
  out.totalProcessingMs =
    static_cast<double>(state_->totalCallbackNs.load(std::memory_order_relaxed)) / 1000000.0;
  out.maxMs = static_cast<double>(state_->maxCallbackNs.load(std::memory_order_relaxed)) / 1000000.0;
  out.budgetMs = state_->budgetMs;
  out.schedulerCaptured = state_->schedulerCaptured.load(std::memory_order_acquire);
  out.schedulerPolicy = state_->schedulerPolicy.load(std::memory_order_relaxed);
  out.schedulerPriority = state_->schedulerPriority.load(std::memory_order_relaxed);
  out.schedulerSetupError = state_->schedulerSetupError.load(std::memory_order_relaxed);
  out.affinitySetupError = state_->affinitySetupError.load(std::memory_order_relaxed);
  if (out.callbacks > 0) {
    out.averageMs = out.totalProcessingMs / static_cast<double>(out.callbacks);
  }
  return out;
}

void MiniaudioBackend::setOutputMuted(bool muted)
{
  desiredOutputMuted_ = muted;
  if (state_) {
    state_->outputMuted.store(muted, std::memory_order_release);
  }
}

std::size_t MiniaudioBackend::readCapturedInput(float* output, std::size_t capacity)
{
  if (!state_ || !output || capacity == 0) return 0;
  const uint64_t written = state_->captureWriteCounter.load(std::memory_order_acquire);
  if (written - state_->captureReadCounter > MiniaudioBackendState::captureMonitorCapacity) {
    state_->captureReadCounter = written - MiniaudioBackendState::captureMonitorCapacity;
  }
  const auto available = static_cast<std::size_t>(written - state_->captureReadCounter);
  const std::size_t count = std::min(available, capacity);
  for (std::size_t i = 0; i < count; ++i) {
    const uint64_t counter = state_->captureReadCounter + i;
    const uint32_t bits = state_->captureMonitor[
      counter % MiniaudioBackendState::captureMonitorCapacity].load(std::memory_order_relaxed);
    output[i] = std::bit_cast<float>(bits);
  }
  state_->captureReadCounter += count;
  return count;
}

void MiniaudioBackend::discardCapturedInput()
{
  if (state_) {
    state_->captureReadCounter = state_->captureWriteCounter.load(std::memory_order_acquire);
  }
}

} // namespace ardor
