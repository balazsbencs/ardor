#include "dsp/ParallelStereoStageExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#endif

namespace ardor {

namespace {

struct TimingCounter {
  std::atomic<std::uint64_t> calls{0};
  std::atomic<std::uint64_t> totalNanoseconds{0};
  std::atomic<std::uint64_t> maximumNanoseconds{0};
  std::atomic<int> actualCpu{-1};
  int requestedCpu = -1;
};

void recordTiming(TimingCounter& timing, std::uint64_t nanoseconds) noexcept
{
  timing.calls.fetch_add(1, std::memory_order_relaxed);
  timing.totalNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
  auto maximum = timing.maximumNanoseconds.load(std::memory_order_relaxed);
  while (maximum < nanoseconds
         && !timing.maximumNanoseconds.compare_exchange_weak(
           maximum, nanoseconds, std::memory_order_relaxed)) {
  }
}

int currentCpu() noexcept
{
#if defined(__linux__)
  return sched_getcpu();
#else
  return -1;
#endif
}

} // namespace

struct ParallelStereoStageExecutor::Impl {
  struct Slot {
    std::vector<float> inputLeft;
    std::vector<float> inputRight;
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    // Published by the callback before waking the worker, then recycled only
    // after `completed` advances. The semaphore/completion handoff orders this
    // metadata together with the slot buffers.
    std::uint64_t generation = 0;
  };

#if defined(__linux__)
  sem_t jobReady{};
  bool semaphoreInitialized = false;
  std::thread worker;
  std::atomic<bool> stopping{false};
  std::atomic<bool> setupComplete{false};
  std::atomic<bool> setupSucceeded{false};
  std::atomic<std::uint64_t> completed{0};
#endif

  ParallelStereoStageExecutorOptions options{};
  ParallelStereoStageProcess process = nullptr;
  void* context = nullptr;
  std::vector<Slot> slots;
  TimingCounter timing;
  bool configured = false;
  bool parallel = false;
  bool ready = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
  std::uint64_t underflows = 0;
  std::uint64_t submissionMisses = 0;
  struct PublishedSlot {
    std::vector<float> left;
    std::vector<float> right;
  };

  // The worker writes completed blocks directly into this second ring. It has
  // one more slot than the input ring, so a later generation cannot reuse the
  // published block while the callback is still consuming the current one.
  // This removes the callback-side ring-to-snapshot copy while keeping all
  // ownership bounded and preallocated.
  std::vector<PublishedSlot> publishedSlots;
  // The callback advances this after it has copied the previous published
  // generation to its host-owned output buffers. A worker waits on the marker
  // before reusing the corresponding publication slot.
  std::atomic<std::uint64_t> publishedConsumedGeneration{0};

  ~Impl() { shutdown(); }

  void shutdown() noexcept
  {
#if defined(__linux__)
    stopping.store(true, std::memory_order_release);
    if (semaphoreInitialized) sem_post(&jobReady);
    if (worker.joinable()) worker.join();
    if (semaphoreInitialized) {
      sem_destroy(&jobReady);
      semaphoreInitialized = false;
    }
#endif
    slots.clear();
    publishedSlots.clear();
    configured = false;
    parallel = false;
    ready = false;
    process = nullptr;
    context = nullptr;
  }

#if defined(__linux__)
  bool configureWorkerScheduling()
  {
    bool success = true;
    if (options.requireRealtimeScheduling) {
      sched_param requested{};
      requested.sched_priority = options.workerPriority;
      if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &requested) != 0) {
        success = false;
      }
    }
    if (options.requireAffinity) {
      if (options.workerCpu < 0 || options.workerCpu >= CPU_SETSIZE) {
        success = false;
      } else {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(options.workerCpu, &cpus);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0) {
          success = false;
        }
      }
    }
#if defined(__aarch64__)
    // Match the existing realtime NAM workers: flush denormals in the worker
    // thread rather than paying for them in every DSP callback.
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (std::uint64_t{1} << 24);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    return success;
  }

  void workerMain()
  {
    const bool setup = configureWorkerScheduling();
    setupSucceeded.store(setup, std::memory_order_relaxed);
    setupComplete.store(true, std::memory_order_release);
    if (options.collectTiming) timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
    while (true) {
      while (sem_wait(&jobReady) != 0 && errno == EINTR) {
      }
      if (stopping.load(std::memory_order_acquire)) break;
      const std::uint64_t generation = completed.load(std::memory_order_acquire) + 1;
      if (generation == 0 || slots.empty()) continue;
      Slot* slot = &slots[static_cast<std::size_t>(generation % slots.size())];
      if (slot->generation == generation && process) {
        float* outputLeft = slot->outputLeft.data();
        float* outputRight = slot->outputRight.data();
        if (!publishedSlots.empty()) {
          const std::uint64_t publicationSlots = publishedSlots.size();
          if (generation > publicationSlots) {
            const std::uint64_t required = generation - publicationSlots;
            while (publishedConsumedGeneration.load(std::memory_order_acquire)
                   < required) {
              if (stopping.load(std::memory_order_acquire)) return;
              std::this_thread::yield();
            }
          }
          auto& published = publishedSlots[static_cast<std::size_t>(generation
            % publishedSlots.size())];
          outputLeft = published.left.data();
          outputRight = published.right.data();
          std::copy(slot->inputLeft.begin(), slot->inputLeft.end(),
                    published.left.begin());
          std::copy(slot->inputRight.begin(), slot->inputRight.end(),
                    published.right.begin());
        } else {
          std::copy(slot->inputLeft.begin(), slot->inputLeft.end(),
                    slot->outputLeft.begin());
          std::copy(slot->inputRight.begin(), slot->inputRight.end(),
                    slot->outputRight.begin());
        }
        const auto start = options.collectTiming
          ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        process(context, outputLeft, outputRight, slot->inputLeft.size());
        if (options.collectTiming) {
          const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
          timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
          recordTiming(timing, static_cast<std::uint64_t>(elapsed));
        }
        completed.store(generation, std::memory_order_release);
      }
    }
  }
#endif

  bool startWorker(std::string& error)
  {
    if (options.mode == ParallelStereoStageExecutionMode::Direct) {
      ready = true;
      parallel = false;
      return true;
    }
#if defined(__linux__)
    if (sem_init(&jobReady, 0, 0) != 0) {
      error = "parallel stereo stage executor could not initialize its worker semaphore";
      return false;
    }
    semaphoreInitialized = true;
    stopping.store(false, std::memory_order_relaxed);
    setupComplete.store(false, std::memory_order_relaxed);
    setupSucceeded.store(false, std::memory_order_relaxed);
    completed.store(0, std::memory_order_relaxed);
    worker = std::thread([this] { workerMain(); });
    while (!setupComplete.load(std::memory_order_acquire)) std::this_thread::yield();
    ready = setupSucceeded.load(std::memory_order_relaxed);
    if (!ready) {
      if (options.requireWorkerSetup) {
        error = "parallel stereo stage worker could not acquire requested realtime scheduling/affinity";
        return false;
      }
      // A pipelined stage has no useful sequential equivalent here: silently
      // moving it back to the callback would invalidate the admission result.
      error = "parallel stereo stage requires a worker in pipelined mode";
      return false;
    }
    parallel = true;
    return true;
#else
    (void)error;
    error = "parallel stereo stage workers are unavailable on this platform";
    return false;
#endif
  }

  bool slotFree(const Slot& slot) const noexcept
  {
    const std::uint64_t generation = slot.generation;
    if (generation == 0) return true;
#if defined(__linux__)
    return completed.load(std::memory_order_acquire) >= generation;
#else
    return false;
#endif
  }

  void publishCompleted()
  {
#if defined(__linux__)
    const std::uint64_t completedGeneration = completed.load(std::memory_order_acquire);
    if (completedGeneration <= outputGeneration || completedGeneration == 0) return;
    const std::size_t slot = static_cast<std::size_t>(completedGeneration % slots.size());
    // `completed` is released after the worker finishes writing the published
    // block. The acquire above therefore establishes ownership without a
    // callback-side ring-to-snapshot copy.
    if (slots[slot].generation == completedGeneration) outputGeneration = completedGeneration;
#endif
  }

  bool processDirect(const float* inputLeft, const float* inputRight,
                     float* outputLeft, float* outputRight, std::size_t frames,
                     ParallelStereoStageProcessResult& result)
  {
    std::copy(inputLeft, inputLeft + frames, outputLeft);
    std::copy(inputRight, inputRight + frames, outputRight);
    const auto start = options.collectTiming
      ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (process) process(context, outputLeft, outputRight, frames);
    if (options.collectTiming) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
      timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
      recordTiming(timing, static_cast<std::uint64_t>(elapsed));
    }
    ++submittedGeneration;
    outputGeneration = submittedGeneration;
    result.accepted = true;
    result.outputReady = true;
    result.workersReady = true;
    result.submittedGeneration = submittedGeneration;
    result.outputGeneration = outputGeneration;
    return true;
  }

  bool processPipelined(const float* inputLeft, const float* inputRight,
                        float* outputLeft, float* outputRight, std::size_t frames,
                        ParallelStereoStageProcessResult& result)
  {
    // Publish the newest completed output before touching the next ring slot.
    // The worker has already written the corresponding published block before
    // releasing `completed`, so this path only copies that block to the
    // host-owned output buffers.
    publishCompleted();

    if (outputGeneration != 0) {
      const auto& published = publishedSlots[static_cast<std::size_t>(outputGeneration
        % publishedSlots.size())];
      std::copy(published.left.begin(), published.left.begin()
                   + static_cast<std::ptrdiff_t>(frames), outputLeft);
      std::copy(published.right.begin(), published.right.begin()
                   + static_cast<std::ptrdiff_t>(frames), outputRight);
      result.outputReady = true;
      result.outputGeneration = outputGeneration;
      // The host-owned output copy is complete, so a worker may now recycle
      // the publication slot on a later generation.
      publishedConsumedGeneration.store(outputGeneration, std::memory_order_release);
    } else {
      ++underflows;
      std::copy(inputLeft, inputLeft + frames, outputLeft);
      std::copy(inputRight, inputRight + frames, outputRight);
      result.usedFallback = true;
    }

    const std::size_t slotIndex = static_cast<std::size_t>(
      (submittedGeneration + 1) % slots.size());
    Slot& slot = slots[slotIndex];
    if (slotFree(slot)) {
      std::copy(inputLeft, inputLeft + frames, slot.inputLeft.begin());
      std::copy(inputRight, inputRight + frames, slot.inputRight.begin());
      const std::uint64_t generation = ++submittedGeneration;
      slot.generation = generation;
#if defined(__linux__)
      sem_post(&jobReady);
#endif
      result.accepted = true;
      result.submittedGeneration = generation;
    } else {
      ++submissionMisses;
      result.submittedGeneration = submittedGeneration;
    }

    return true;
  }
};

ParallelStereoStageExecutor::ParallelStereoStageExecutor()
  : impl_(std::make_unique<Impl>())
{
}

ParallelStereoStageExecutor::~ParallelStereoStageExecutor() = default;

bool ParallelStereoStageExecutor::configure(ParallelStereoStageProcess process,
                                             void* context,
                                             ParallelStereoStageExecutorOptions options,
                                             std::string& error)
{
  error.clear();
  impl_->shutdown();
  impl_->options = options;
  impl_->process = process;
  impl_->context = context;
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->underflows = 0;
  impl_->submissionMisses = 0;
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);
  impl_->timing.requestedCpu = options.workerCpu;
  impl_->timing.calls.store(0, std::memory_order_relaxed);
  impl_->timing.totalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.maximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.actualCpu.store(-1, std::memory_order_relaxed);
  if (!process) {
    error = "parallel stereo stage executor requires a process callback";
    return false;
  }
  if (options.blockSize == 0 || !std::isfinite(options.sampleRate)
      || options.sampleRate <= 0.0) {
    error = "parallel stereo stage executor requires a valid block size and sample rate";
    return false;
  }
  if (options.mode == ParallelStereoStageExecutionMode::Pipelined
      && options.pipelineSlots < 2) {
    error = "pipelined stereo stage executor requires at least two slots";
    return false;
  }

  const std::size_t slotCount = options.mode == ParallelStereoStageExecutionMode::Pipelined
    ? options.pipelineSlots : 1;
  impl_->slots.resize(slotCount);
  for (auto& slot : impl_->slots) {
    slot.inputLeft.assign(options.blockSize, 0.0f);
    slot.inputRight.assign(options.blockSize, 0.0f);
    slot.outputLeft.assign(options.blockSize, 0.0f);
    slot.outputRight.assign(options.blockSize, 0.0f);
  }
  const std::size_t publishedSlotCount = options.mode
    == ParallelStereoStageExecutionMode::Pipelined ? options.pipelineSlots + 1 : 1;
  impl_->publishedSlots.resize(publishedSlotCount);
  for (auto& published : impl_->publishedSlots) {
    published.left.assign(options.blockSize, 0.0f);
    published.right.assign(options.blockSize, 0.0f);
  }
  if (!impl_->startWorker(error)) {
    impl_->shutdown();
    return false;
  }
  impl_->configured = true;
  return true;
}

bool ParallelStereoStageExecutor::processBlock(
  const float* inputLeft, const float* inputRight, float* outputLeft,
  float* outputRight, std::size_t frames,
  ParallelStereoStageProcessResult& result)
{
  result = {};
  if (!impl_->configured || !inputLeft || !inputRight || !outputLeft || !outputRight
      || frames != impl_->options.blockSize) return false;
  result.workersReady = impl_->ready;
  if (impl_->options.mode == ParallelStereoStageExecutionMode::Pipelined) {
    return impl_->processPipelined(inputLeft, inputRight, outputLeft, outputRight,
                                   frames, result);
  }
  return impl_->processDirect(inputLeft, inputRight, outputLeft, outputRight,
                              frames, result);
}

bool ParallelStereoStageExecutor::configured() const noexcept
{
  return impl_->configured;
}

bool ParallelStereoStageExecutor::parallelEnabled() const noexcept
{
  return impl_->parallel;
}

bool ParallelStereoStageExecutor::workersReady() const noexcept
{
  return impl_->ready;
}

std::size_t ParallelStereoStageExecutor::blockSize() const noexcept
{
  return impl_->options.blockSize;
}

std::uint64_t ParallelStereoStageExecutor::underflowCount() const noexcept
{
  return impl_->underflows;
}

std::uint64_t ParallelStereoStageExecutor::submissionMissCount() const noexcept
{
  return impl_->submissionMisses;
}

ParallelStereoStageTimingSnapshot ParallelStereoStageExecutor::timing() const noexcept
{
  return {
    impl_->timing.calls.load(std::memory_order_relaxed),
    impl_->timing.totalNanoseconds.load(std::memory_order_relaxed),
    impl_->timing.maximumNanoseconds.load(std::memory_order_relaxed),
    impl_->timing.requestedCpu,
    impl_->timing.actualCpu.load(std::memory_order_relaxed),
  };
}

void ParallelStereoStageExecutor::reset() noexcept
{
  if (!impl_->configured) return;
  // Reset invalidates all callback-visible outputs. Release every submitted
  // publication generation before waiting for a worker parked on reuse.
  impl_->publishedConsumedGeneration.store(impl_->submittedGeneration,
                                           std::memory_order_release);
#if defined(__linux__)
  if (impl_->options.mode == ParallelStereoStageExecutionMode::Pipelined) {
    const std::uint64_t wanted = impl_->submittedGeneration;
    while (impl_->completed.load(std::memory_order_acquire) < wanted) {
      std::this_thread::yield();
    }
  }
#endif
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->underflows = 0;
  impl_->submissionMisses = 0;
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);
  impl_->timing.calls.store(0, std::memory_order_relaxed);
  impl_->timing.totalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.maximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.actualCpu.store(-1, std::memory_order_relaxed);
  for (auto& slot : impl_->slots) {
    slot.generation = 0;
    std::fill(slot.inputLeft.begin(), slot.inputLeft.end(), 0.0f);
    std::fill(slot.inputRight.begin(), slot.inputRight.end(), 0.0f);
    std::fill(slot.outputLeft.begin(), slot.outputLeft.end(), 0.0f);
    std::fill(slot.outputRight.begin(), slot.outputRight.end(), 0.0f);
  }
  for (auto& published : impl_->publishedSlots) {
    std::fill(published.left.begin(), published.left.end(), 0.0f);
    std::fill(published.right.begin(), published.right.end(), 0.0f);
  }
#if defined(__linux__)
  impl_->completed.store(0, std::memory_order_relaxed);
#endif
}

void ParallelStereoStageExecutor::clear() noexcept
{
  impl_->shutdown();
  impl_->options = {};
  impl_->slots.clear();
  impl_->publishedSlots.clear();
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->underflows = 0;
  impl_->submissionMisses = 0;
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);
  impl_->timing.requestedCpu = -1;
  impl_->timing.calls.store(0, std::memory_order_relaxed);
  impl_->timing.totalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.maximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->timing.actualCpu.store(-1, std::memory_order_relaxed);
}

} // namespace ardor
