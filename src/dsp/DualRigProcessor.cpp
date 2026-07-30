#include "dsp/DualRigProcessor.h"

#include "dsp/RuntimeChain.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <thread>
#endif

namespace ardor {

struct DualRigProcessor::ParallelState {
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

DualRigProcessor::DualRigProcessor() = default;
DualRigProcessor::~DualRigProcessor()
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

bool DualRigProcessor::configure(DualRigLaneConfig left, DualRigLaneConfig right,
                                 NamInputMode inputMode, double sampleRate,
                                 std::size_t blockSize, bool requestParallel,
                                 int workerCpu, std::string& error)
{
  error.clear();
  if (!left.chain || !right.chain || blockSize == 0
      || !std::isfinite(sampleRate) || sampleRate <= 0.0) {
    error = "dual rig requires two prepared lanes and a valid audio format";
    return false;
  }

  inputMode_ = inputMode;
  sampleRate_ = sampleRate;
  blockSize_ = blockSize;
  left_.chain = std::move(left.chain);
  right_.chain = std::move(right.chain);
  left_.level = std::isfinite(left.level) ? std::max(0.0f, left.level) : 1.0f;
  right_.level = std::isfinite(right.level) ? std::max(0.0f, right.level) : 1.0f;
  left_.polarity = left.polarityInverted ? -1.0f : 1.0f;
  right_.polarity = right.polarityInverted ? -1.0f : 1.0f;
  prepareBlockSize(blockSize_);

#if defined(__linux__)
  if (requestParallel && workerCpu < 0) {
    std::cerr << "Dual Rig parallel processing requires a dedicated worker CPU; "
                 "using sequential processing.\n";
  } else if (requestParallel) {
    constexpr int kWorkerPriority = 69;
    auto state = std::make_unique<ParallelState>();
    state->requestedCpu = workerCpu;
    if (sem_init(&state->jobReady, 0, 0) == 0) {
      ParallelState* raw = state.get();
      raw->worker = std::thread([this, raw]() {
        bool setupSucceeded = true;
        sched_param requested{};
        requested.sched_priority = kWorkerPriority;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &requested) != 0) {
          setupSucceeded = false;
        }
        if (raw->requestedCpu < 0 || raw->requestedCpu >= CPU_SETSIZE) {
          setupSucceeded = false;
        } else {
          cpu_set_t cpus;
          CPU_ZERO(&cpus);
          CPU_SET(raw->requestedCpu, &cpus);
          if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0) {
            setupSucceeded = false;
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
          processLaneBlock(right_, true, raw->input, raw->output, raw->frames);
          raw->completed.store(generation, std::memory_order_release);
        }
      });
      while (!raw->setupComplete.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      if (raw->setupSucceeded.load(std::memory_order_relaxed)) {
        parallel_ = state.release();
        std::cerr << "Dual Rig parallel lanes enabled: complete right lane on CPU "
                  << workerCpu << ".\n";
      } else {
        std::cerr << "Dual Rig worker could not acquire requested realtime scheduling/affinity; "
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

void DualRigProcessor::prepareBlockSize(std::size_t frames)
{
  if (frames == 0) return;
  blockSize_ = frames;
  monoInput_.assign(frames, 0.0f);
  left_.firstOutput.assign(frames, 0.0f);
  left_.secondOutput.assign(frames, 0.0f);
  right_.firstOutput.assign(frames, 0.0f);
  right_.secondOutput.assign(frames, 0.0f);
  if (left_.chain) left_.chain->prepareBlockSize(frames);
  if (right_.chain) right_.chain->prepareBlockSize(frames);
}

void DualRigProcessor::processLaneBlock(Lane& lane, bool takeRightOutput,
                                        const float* input, float* output,
                                        std::size_t frames)
{
  lane.chain->processBlock(input, lane.firstOutput.data(), lane.secondOutput.data(), frames);
  const float* selected = takeRightOutput ? lane.secondOutput.data() : lane.firstOutput.data();
  const float gain = lane.level * lane.polarity;
  for (std::size_t i = 0; i < frames; ++i) output[i] = selected[i] * gain;
}

float DualRigProcessor::processLaneSample(Lane& lane, bool takeRightOutput, float input)
{
  const auto result = lane.chain->process({input, input});
  return (takeRightOutput ? result.right : result.left) * lane.level * lane.polarity;
}

void DualRigProcessor::processBlock(const float* inputLeft, const float* inputRight,
                                    float* outputLeft, float* outputRight,
                                    std::size_t frames)
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
    processLaneBlock(left_, false, monoInput_.data(), outputLeft, frames);
    while (parallel_->completed.load(std::memory_order_acquire) != generation) {
      // Both realtime threads are pinned to separate product-configured cores.
    }
    if (std::chrono::duration<double>(
          std::chrono::steady_clock::now() - blockStart).count()
        > static_cast<double>(frames) / sampleRate_) {
      parallel_->waitsOverBudget.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
#endif

  processLaneBlock(left_, false, monoInput_.data(), outputLeft, frames);
  processLaneBlock(right_, true, monoInput_.data(), outputRight, frames);
}

void DualRigProcessor::process(float inputLeft, float inputRight,
                               float& outputLeft, float& outputRight)
{
  const float mono = routeNamInput(inputMode_, inputLeft, inputRight);
  outputLeft = processLaneSample(left_, false, mono);
  outputRight = processLaneSample(right_, true, mono);
}

void DualRigProcessor::reset()
{
  left_.chain->reset();
  right_.chain->reset();
}

std::size_t DualRigProcessor::tailFrames() const noexcept
{
  return std::max(left_.chain->tailFrames(), right_.chain->tailFrames());
}

bool DualRigProcessor::parallelEnabled() const noexcept
{
  return parallel_ != nullptr;
}

uint64_t DualRigProcessor::parallelWaitOverBudgetCount() const noexcept
{
#if defined(__linux__)
  return parallel_ ? parallel_->waitsOverBudget.load(std::memory_order_relaxed) : 0;
#else
  return 0;
#endif
}

} // namespace ardor
