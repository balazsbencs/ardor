#include "dsp/DualAmpProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <memory>
#include <iostream>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <thread>
#endif

namespace ardor {

namespace {

constexpr int kDualAmpWorkerPriority = 69;

} // namespace

struct DualAmpProcessor::ParallelState {
#if defined(__linux__)
  sem_t jobReady{};
  std::thread worker;
  std::atomic<bool> stopping{false};
  std::atomic<bool> setupComplete{false};
  std::atomic<bool> setupSucceeded{false};
  std::atomic<uint64_t> submitted{0};
  std::atomic<uint64_t> completed{0};
  std::atomic<uint64_t> waitsOverBudget{0};
  const float* input = nullptr;
  float* output = nullptr;
  std::size_t frames = 0;
  int requestedCpu = -1;
#endif
};

DualAmpProcessor::DualAmpProcessor() = default;

DualAmpProcessor::~DualAmpProcessor()
{
#if defined(__linux__)
  if (parallel_) {
    parallel_->stopping.store(true, std::memory_order_release);
    sem_post(&parallel_->jobReady);
    if (parallel_->worker.joinable()) parallel_->worker.join();
    sem_destroy(&parallel_->jobReady);
  }
#endif
  delete parallel_;
}

bool DualAmpProcessor::configure(DualAmpLaneConfig left, DualAmpLaneConfig right,
                                 NamInputMode inputMode, double sampleRate, int maxBlockSize,
                                 bool requestParallel, int workerCpu, std::string& error)
{
  error.clear();
  sampleRate_ = sampleRate;
  inputMode_ = inputMode;
  blockSize_ = static_cast<std::size_t>(std::max(1, maxBlockSize));

  const auto loadLane = [&](Lane& lane, DualAmpLaneConfig&& config, const char* name) {
    if (!lane.nam.load(config.modelPath, sampleRate, maxBlockSize, config.slimmableSize)) {
      error = std::string{"failed to load dual amp "} + name + " NAM: " + config.modelPath.string();
      return false;
    }
    lane.cab.loadImpulse(std::move(config.impulse));
    lane.cab.prepareBlockSize(blockSize_);
    lane.cabLevel = std::isfinite(config.cabLevel) ? std::max(0.0f, config.cabLevel) : 1.0f;
    lane.cabMix = std::isfinite(config.cabMix) ? std::clamp(config.cabMix, 0.0f, 1.0f) : 1.0f;
    lane.polarity = config.polarityInverted ? -1.0f : 1.0f;
    lane.namOutput.assign(blockSize_, 0.0f);
    lane.cabOutput.assign(blockSize_, 0.0f);
    return true;
  };

  if (!loadLane(left_, std::move(left), "left")
      || !loadLane(right_, std::move(right), "right")) {
    return false;
  }
  monoInput_.assign(blockSize_, 0.0f);

#if defined(__linux__)
  if (requestParallel && workerCpu < 0) {
    std::cerr << "Dual Amp parallel processing requires a dedicated worker CPU; "
                 "using sequential processing.\n";
  } else if (requestParallel) {
    auto state = std::make_unique<ParallelState>();
    state->requestedCpu = workerCpu;
    if (sem_init(&state->jobReady, 0, 0) == 0) {
      ParallelState* raw = state.get();
      raw->worker = std::thread([this, raw]() {
        bool setupSucceeded = true;
        sched_param requested{};
        requested.sched_priority = kDualAmpWorkerPriority;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &requested) != 0) {
          setupSucceeded = false;
        }
        if (raw->requestedCpu >= 0) {
          if (raw->requestedCpu >= CPU_SETSIZE) {
            setupSucceeded = false;
          } else {
            cpu_set_t cpus;
            CPU_ZERO(&cpus);
            CPU_SET(raw->requestedCpu, &cpus);
            if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0) {
              setupSucceeded = false;
            }
          }
        }
#if defined(__aarch64__)
        uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (uint64_t{1} << 24);
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
        raw->setupSucceeded.store(setupSucceeded, std::memory_order_relaxed);
        raw->setupComplete.store(true, std::memory_order_release);

        while (true) {
          while (sem_wait(&raw->jobReady) != 0 && errno == EINTR) {
          }
          if (raw->stopping.load(std::memory_order_acquire)) break;
          const uint64_t generation = raw->submitted.load(std::memory_order_acquire);
          processLaneBlock(right_, raw->input, raw->output, raw->frames);
          raw->completed.store(generation, std::memory_order_release);
        }
      });
      while (!raw->setupComplete.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      if (raw->setupSucceeded.load(std::memory_order_relaxed)) {
        parallel_ = state.release();
        std::cerr << "Dual Amp parallel lanes enabled: callback lane left, worker lane right"
                  << (workerCpu >= 0 ? " on CPU " + std::to_string(workerCpu) : "")
                  << ".\n";
      } else {
        std::cerr << "Dual Amp worker could not acquire requested realtime scheduling/affinity; "
                     "using sequential processing.\n";
        raw->stopping.store(true, std::memory_order_release);
        sem_post(&raw->jobReady);
        raw->worker.join();
        sem_destroy(&raw->jobReady);
      }
    }
  }
#else
  (void)requestParallel;
  (void)workerCpu;
#endif

  return true;
}

void DualAmpProcessor::prepareBlockSize(std::size_t frames)
{
  if (frames == 0 || blockSize_ == frames) return;
  blockSize_ = frames;
  monoInput_.assign(frames, 0.0f);
  left_.namOutput.assign(frames, 0.0f);
  left_.cabOutput.assign(frames, 0.0f);
  right_.namOutput.assign(frames, 0.0f);
  right_.cabOutput.assign(frames, 0.0f);
  left_.cab.prepareBlockSize(frames);
  right_.cab.prepareBlockSize(frames);
}

void DualAmpProcessor::processLaneBlock(Lane& lane, const float* input,
                                        float* output, std::size_t frames)
{
  lane.nam.processBlock(input, lane.namOutput.data(), frames);
  lane.cab.processBlock(lane.namOutput.data(), lane.cabOutput.data(), frames);
  const float mix = lane.cabMix;
  const float dry = 1.0f - mix;
  const float wetLevel = lane.cabLevel;
  for (std::size_t i = 0; i < frames; ++i) {
    output[i] = (lane.cabOutput[i] * wetLevel * mix + lane.namOutput[i] * dry) * lane.polarity;
  }
}

float DualAmpProcessor::processLaneSample(Lane& lane, float input)
{
  const float nam = lane.nam.process(input);
  const float cab = lane.cab.processSample(nam) * lane.cabLevel;
  return (cab * lane.cabMix + nam * (1.0f - lane.cabMix)) * lane.polarity;
}

void DualAmpProcessor::processBlock(const float* inputLeft, const float* inputRight,
                                    float* outputLeft, float* outputRight, std::size_t frames)
{
  if (frames == 0) return;
  if (blockSize_ != frames) prepareBlockSize(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    monoInput_[i] = routeNamInput(inputMode_, inputLeft[i], inputRight[i]);
  }

#if defined(__linux__)
  if (parallel_) {
    const uint64_t generation = parallel_->submitted.load(std::memory_order_relaxed) + 1;
    parallel_->input = monoInput_.data();
    parallel_->output = outputRight;
    parallel_->frames = frames;
    parallel_->submitted.store(generation, std::memory_order_release);
    sem_post(&parallel_->jobReady);

    const auto blockStart = std::chrono::steady_clock::now();
    processLaneBlock(left_, monoInput_.data(), outputLeft, frames);
    while (parallel_->completed.load(std::memory_order_acquire) != generation) {
      // Both DSP threads are SCHED_FIFO; product configuration also pins them
      // to separate Pi cores.
    }
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - blockStart).count()
        > static_cast<double>(frames) / sampleRate_) {
      parallel_->waitsOverBudget.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
#endif

  processLaneBlock(left_, monoInput_.data(), outputLeft, frames);
  processLaneBlock(right_, monoInput_.data(), outputRight, frames);
}

void DualAmpProcessor::process(float inputLeft, float inputRight,
                               float& outputLeft, float& outputRight)
{
  const float mono = routeNamInput(inputMode_, inputLeft, inputRight);
  outputLeft = processLaneSample(left_, mono);
  outputRight = processLaneSample(right_, mono);
}

void DualAmpProcessor::reset()
{
  left_.nam.reset();
  left_.cab.reset();
  right_.nam.reset();
  right_.cab.reset();
}

std::size_t DualAmpProcessor::tailFrames() const noexcept
{
  return std::max(left_.cab.tailFrames(), right_.cab.tailFrames());
}

bool DualAmpProcessor::parallelEnabled() const noexcept
{
  return parallel_ != nullptr;
}

uint64_t DualAmpProcessor::parallelWaitOverBudgetCount() const noexcept
{
#if defined(__linux__)
  return parallel_ ? parallel_->waitsOverBudget.load(std::memory_order_relaxed) : 0;
#else
  return 0;
#endif
}

} // namespace ardor
