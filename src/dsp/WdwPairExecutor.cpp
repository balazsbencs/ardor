#include "dsp/WdwPairExecutor.h"

#include <algorithm>
#include <array>
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

struct WdwPairExecutor::Impl {
  struct Slot {
    std::vector<float> input;
    std::vector<float> dryLeft;
    std::vector<float> dryRight;
    std::vector<float> wetLeft;
    std::vector<float> wetRight;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint8_t> completionMask{0};
    std::atomic<std::uint64_t> consumedGeneration{0};
  };

  struct Worker {
    WdwPairLane lane{};
    std::size_t laneIndex = 0;
    int requestedCpu = -1;
    TimingCounter timing;
#if defined(__linux__)
    sem_t jobReady{};
    bool semaphoreInitialized = false;
    std::thread thread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> setupComplete{false};
    std::atomic<bool> setupSucceeded{false};
    std::atomic<std::uint64_t> completed{0};
#endif
  };

  WdwPairExecutorOptions options{};
  std::array<std::unique_ptr<Worker>, 2> workers{};
  std::unique_ptr<Slot[]> slots;
  std::size_t slotCount = 0;
  bool configured = false;
  bool parallel = false;
  bool ready = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
  std::size_t outputAgeBlocks = 0;
  std::uint64_t underflows = 0;
  std::uint64_t submissionMisses = 0;
  bool haveLastPair = false;
  std::vector<float> lastDryLeft;
  std::vector<float> lastDryRight;
  std::vector<float> lastWetLeft;
  std::vector<float> lastWetRight;

  ~Impl() { shutdown(); }

  void shutdown() noexcept
  {
#if defined(__linux__)
    for (auto& worker : workers) {
      if (!worker) continue;
      worker->stopping.store(true, std::memory_order_release);
      if (worker->semaphoreInitialized) sem_post(&worker->jobReady);
    }
    for (auto& worker : workers) {
      if (!worker) continue;
      if (worker->thread.joinable()) worker->thread.join();
      if (worker->semaphoreInitialized) {
        sem_destroy(&worker->jobReady);
        worker->semaphoreInitialized = false;
      }
    }
#endif
    workers = {};
    slots.reset();
    slotCount = 0;
    lastDryLeft.clear();
    lastDryRight.clear();
    lastWetLeft.clear();
    lastWetRight.clear();
    configured = false;
    parallel = false;
    ready = false;
    submittedGeneration = 0;
    outputGeneration = 0;
    outputAgeBlocks = 0;
    underflows = 0;
    submissionMisses = 0;
    haveLastPair = false;
  }

#if defined(__linux__)
  bool configureWorkerScheduling(const Worker& worker)
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
      if (worker.requestedCpu < 0 || worker.requestedCpu >= CPU_SETSIZE) {
        success = false;
      } else {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(worker.requestedCpu, &cpus);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0) {
          success = false;
        }
      }
    }
#if defined(__aarch64__)
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (std::uint64_t{1} << 24);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    return success;
  }

  void workerMain(Worker& worker)
  {
    const bool setup = configureWorkerScheduling(worker);
    worker.setupSucceeded.store(setup, std::memory_order_relaxed);
    worker.setupComplete.store(true, std::memory_order_release);
    if (options.collectTiming) {
      worker.timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
    }

    while (true) {
      while (sem_wait(&worker.jobReady) != 0 && errno == EINTR) {
      }
      if (worker.stopping.load(std::memory_order_acquire)) return;

      const std::uint64_t generation =
        worker.completed.load(std::memory_order_acquire) + 1;
      if (generation == 0 || !slots || slotCount == 0) continue;
      Slot& slot = slots[static_cast<std::size_t>(generation % slotCount)];
      if (slot.generation.load(std::memory_order_acquire) != generation
          || !worker.lane.process) {
        continue;
      }

      float* outputLeft = worker.laneIndex == 0
        ? slot.dryLeft.data() : slot.wetLeft.data();
      float* outputRight = worker.laneIndex == 0
        ? slot.dryRight.data() : slot.wetRight.data();
      const auto start = options.collectTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
      worker.lane.process(worker.lane.context, slot.input.data(), outputLeft,
                          outputRight, slot.input.size());
      if (options.collectTiming) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
        worker.timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
        recordTiming(worker.timing, static_cast<std::uint64_t>(elapsed));
      }
      slot.completionMask.fetch_or(
        static_cast<std::uint8_t>(1U << worker.laneIndex),
        std::memory_order_release);
      worker.completed.store(generation, std::memory_order_release);
    }
  }
#endif

  bool startWorkers(std::string& error)
  {
    if (options.mode == WdwPairExecutionMode::Direct) {
      ready = true;
      parallel = false;
      return true;
    }
#if defined(__linux__)
    for (std::size_t lane = 0; lane < workers.size(); ++lane) {
      auto& worker = workers[lane];
      if (sem_init(&worker->jobReady, 0, 0) != 0) {
        error = "WDW pair executor could not initialize its worker semaphore";
        return false;
      }
      worker->semaphoreInitialized = true;
      worker->stopping.store(false, std::memory_order_relaxed);
      worker->setupComplete.store(false, std::memory_order_relaxed);
      worker->setupSucceeded.store(false, std::memory_order_relaxed);
      worker->completed.store(0, std::memory_order_relaxed);
      Worker* raw = worker.get();
      worker->thread = std::thread([this, raw] { workerMain(*raw); });
    }
    for (const auto& worker : workers) {
      while (!worker->setupComplete.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }
    ready = true;
    for (const auto& worker : workers) {
      if (!worker->setupSucceeded.load(std::memory_order_relaxed)) ready = false;
    }
    if (!ready && options.requireWorkerSetup) {
      error = "WDW pair worker could not acquire requested realtime scheduling/affinity";
      return false;
    }
    parallel = true;
    return true;
#else
    (void)error;
    error = "WDW pair workers are unavailable on this platform";
    return false;
#endif
  }

  bool slotFree(const Slot& slot) const noexcept
  {
    const std::uint64_t generation = slot.generation.load(std::memory_order_acquire);
    if (generation == 0) return true;
    return slot.completionMask.load(std::memory_order_acquire) == 0x3U
      && slot.consumedGeneration.load(std::memory_order_acquire) >= generation;
  }

  std::uint64_t publishCompletePair()
  {
    if (!parallel || !slots || slotCount == 0 || submittedGeneration == 0) return 0;
    const std::uint64_t firstLive = submittedGeneration >= slotCount
      ? submittedGeneration - slotCount + 1 : 1;
    const std::uint64_t firstNew = std::max(firstLive, outputGeneration + 1);
    std::uint64_t selected = 0;
    for (std::uint64_t generation = submittedGeneration;; --generation) {
      Slot& slot = slots[static_cast<std::size_t>(generation % slotCount)];
      if (slot.generation.load(std::memory_order_acquire) == generation
          && slot.completionMask.load(std::memory_order_acquire) == 0x3U) {
        selected = generation;
        break;
      }
      if (generation == firstNew) break;
    }
    if (selected == 0) return 0;

    Slot& chosen = slots[static_cast<std::size_t>(selected % slotCount)];
    std::copy(chosen.dryLeft.begin(), chosen.dryLeft.end(), lastDryLeft.begin());
    std::copy(chosen.dryRight.begin(), chosen.dryRight.end(), lastDryRight.begin());
    std::copy(chosen.wetLeft.begin(), chosen.wetLeft.end(), lastWetLeft.begin());
    std::copy(chosen.wetRight.begin(), chosen.wetRight.end(), lastWetRight.begin());
    outputGeneration = selected;
    outputAgeBlocks = 0;
    haveLastPair = true;

    // Discard every older complete generation in the live window.  A newer
    // pair supersedes it; acknowledging it here prevents an old complete slot
    // from pinning the bounded ring while a different worker catches up.
    for (std::uint64_t generation = firstLive; generation <= selected; ++generation) {
      Slot& slot = slots[static_cast<std::size_t>(generation % slotCount)];
      if (slot.generation.load(std::memory_order_acquire) == generation
          && slot.completionMask.load(std::memory_order_acquire) == 0x3U) {
        slot.consumedGeneration.store(generation, std::memory_order_release);
      }
    }
    return selected;
  }

  static void copyPair(const std::vector<float>& dryLeft,
                       const std::vector<float>& dryRight,
                       const std::vector<float>& wetLeft,
                       const std::vector<float>& wetRight,
                       float* outputDryLeft, float* outputDryRight,
                       float* outputWetLeft, float* outputWetRight)
  {
    std::copy(dryLeft.begin(), dryLeft.end(), outputDryLeft);
    std::copy(dryRight.begin(), dryRight.end(), outputDryRight);
    std::copy(wetLeft.begin(), wetLeft.end(), outputWetLeft);
    std::copy(wetRight.begin(), wetRight.end(), outputWetRight);
  }

  bool processDirect(const float* input, float* dryLeft, float* dryRight,
                     float* wetLeft, float* wetRight, std::size_t frames,
                     WdwPairProcessResult& result)
  {
    Slot& slot = slots[0];
    std::copy(input, input + frames, slot.input.begin());
    const std::uint64_t generation = ++submittedGeneration;
    slot.generation.store(generation, std::memory_order_release);
    slot.completionMask.store(0x3U, std::memory_order_relaxed);

    for (std::size_t lane = 0; lane < workers.size(); ++lane) {
      Worker& worker = *workers[lane];
      float* outputLeft = lane == 0 ? slot.dryLeft.data() : slot.wetLeft.data();
      float* outputRight = lane == 0 ? slot.dryRight.data() : slot.wetRight.data();
      const auto start = options.collectTiming
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
      worker.lane.process(worker.lane.context, slot.input.data(), outputLeft,
                          outputRight, frames);
      if (options.collectTiming) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
        worker.timing.actualCpu.store(currentCpu(), std::memory_order_relaxed);
        recordTiming(worker.timing, static_cast<std::uint64_t>(elapsed));
      }
    }

    copyPair(slot.dryLeft, slot.dryRight, slot.wetLeft, slot.wetRight,
             dryLeft, dryRight, wetLeft, wetRight);
    copyPair(slot.dryLeft, slot.dryRight, slot.wetLeft, slot.wetRight,
             lastDryLeft.data(), lastDryRight.data(), lastWetLeft.data(),
             lastWetRight.data());
    slot.consumedGeneration.store(generation, std::memory_order_release);
    outputGeneration = generation;
    outputAgeBlocks = 0;
    haveLastPair = true;
    result.accepted = true;
    result.outputReady = true;
    result.pairReady = true;
    result.workersReady = true;
    result.submittedGeneration = generation;
    result.outputGeneration = generation;
    return true;
  }

  bool processPipelined(const float* input, float* dryLeft, float* dryRight,
                        float* wetLeft, float* wetRight, std::size_t frames,
                        WdwPairProcessResult& result)
  {
    const std::uint64_t completed = publishCompletePair();
    if (completed != 0) {
      copyPair(lastDryLeft, lastDryRight, lastWetLeft, lastWetRight,
               dryLeft, dryRight, wetLeft, wetRight);
      result.outputReady = true;
      result.pairReady = true;
      result.outputGeneration = outputGeneration;
    } else if (haveLastPair && outputAgeBlocks < options.maxHoldBlocks) {
      ++outputAgeBlocks;
      copyPair(lastDryLeft, lastDryRight, lastWetLeft, lastWetRight,
               dryLeft, dryRight, wetLeft, wetRight);
      result.outputReady = true;
      result.usedFallback = true;
      result.heldLastPair = true;
      result.outputGeneration = outputGeneration;
      result.outputAgeBlocks = outputAgeBlocks;
      ++underflows;
    } else {
      std::fill(dryLeft, dryLeft + frames, 0.0f);
      std::fill(dryRight, dryRight + frames, 0.0f);
      std::fill(wetLeft, wetLeft + frames, 0.0f);
      std::fill(wetRight, wetRight + frames, 0.0f);
      result.usedFallback = true;
      result.outputAgeBlocks = haveLastPair ? ++outputAgeBlocks : 0;
      ++underflows;
    }

    const std::uint64_t generation = submittedGeneration + 1;
    Slot& slot = slots[static_cast<std::size_t>(generation % slotCount)];
    if (!slotFree(slot)) {
      ++submissionMisses;
      result.submittedGeneration = submittedGeneration;
      result.workersReady = ready;
      return true;
    }

    std::copy(input, input + frames, slot.input.begin());
    slot.completionMask.store(0, std::memory_order_relaxed);
    slot.consumedGeneration.store(0, std::memory_order_relaxed);
    slot.generation.store(generation, std::memory_order_release);
    ++submittedGeneration;
#if defined(__linux__)
    sem_post(&workers[0]->jobReady);
    sem_post(&workers[1]->jobReady);
#endif
    result.accepted = true;
    result.submittedGeneration = generation;
    result.workersReady = ready;
    return true;
  }
};

WdwPairExecutor::WdwPairExecutor() : impl_(std::make_unique<Impl>()) {}
WdwPairExecutor::~WdwPairExecutor() = default;

bool WdwPairExecutor::configure(WdwPairLane dry, WdwPairLane wet,
                                 WdwPairExecutorOptions options,
                                 std::string& error)
{
  error.clear();
  impl_->shutdown();
  impl_->options = options;
  if (!dry.process || !wet.process) {
    error = "WDW pair executor requires both lane callbacks";
    return false;
  }
  if (options.blockSize == 0 || !std::isfinite(options.sampleRate)
      || options.sampleRate <= 0.0) {
    error = "WDW pair executor requires a valid block size and sample rate";
    return false;
  }
  if (options.mode == WdwPairExecutionMode::Pipelined
      && options.pipelineSlots < 2) {
    error = "WDW pair executor requires at least two pipeline slots";
    return false;
  }
#if !defined(__linux__)
  if (options.mode == WdwPairExecutionMode::Pipelined) {
    error = "WDW pair executor workers are unavailable on this platform";
    return false;
  }
#endif

  impl_->workers[0] = std::make_unique<Impl::Worker>();
  impl_->workers[1] = std::make_unique<Impl::Worker>();
  impl_->workers[0]->lane = dry;
  impl_->workers[1]->lane = wet;
  impl_->workers[0]->laneIndex = 0;
  impl_->workers[1]->laneIndex = 1;
  impl_->workers[0]->requestedCpu = dry.workerCpu;
  impl_->workers[1]->requestedCpu = wet.workerCpu;
  impl_->workers[0]->timing.requestedCpu = dry.workerCpu;
  impl_->workers[1]->timing.requestedCpu = wet.workerCpu;

  impl_->slotCount = options.mode == WdwPairExecutionMode::Pipelined
    ? options.pipelineSlots : 1;
  impl_->slots = std::make_unique<Impl::Slot[]>(impl_->slotCount);
  for (std::size_t slot = 0; slot < impl_->slotCount; ++slot) {
    auto& current = impl_->slots[slot];
    current.input.assign(options.blockSize, 0.0f);
    current.dryLeft.assign(options.blockSize, 0.0f);
    current.dryRight.assign(options.blockSize, 0.0f);
    current.wetLeft.assign(options.blockSize, 0.0f);
    current.wetRight.assign(options.blockSize, 0.0f);
  }
  impl_->lastDryLeft.assign(options.blockSize, 0.0f);
  impl_->lastDryRight.assign(options.blockSize, 0.0f);
  impl_->lastWetLeft.assign(options.blockSize, 0.0f);
  impl_->lastWetRight.assign(options.blockSize, 0.0f);

  if (!impl_->startWorkers(error)) {
    impl_->shutdown();
    return false;
  }
  impl_->configured = true;
  return true;
}

bool WdwPairExecutor::processBlock(const float* input, float* dryLeft,
                                    float* dryRight, float* wetLeft,
                                    float* wetRight, std::size_t frames,
                                    WdwPairProcessResult& result)
{
  result = {};
  if (!impl_->configured || !input || !dryLeft || !dryRight || !wetLeft || !wetRight
      || frames != impl_->options.blockSize) return false;
  if (impl_->options.mode == WdwPairExecutionMode::Direct) {
    return impl_->processDirect(input, dryLeft, dryRight, wetLeft, wetRight,
                                frames, result);
  }
  return impl_->processPipelined(input, dryLeft, dryRight, wetLeft, wetRight,
                                 frames, result);
}

bool WdwPairExecutor::configured() const noexcept { return impl_->configured; }
bool WdwPairExecutor::parallelEnabled() const noexcept { return impl_->parallel; }
bool WdwPairExecutor::workersReady() const noexcept { return impl_->ready; }
std::size_t WdwPairExecutor::blockSize() const noexcept { return impl_->options.blockSize; }
std::uint64_t WdwPairExecutor::underflowCount() const noexcept { return impl_->underflows; }
std::uint64_t WdwPairExecutor::submissionMissCount() const noexcept
{
  return impl_->submissionMisses;
}

WdwPairTimingSnapshot WdwPairExecutor::timing(std::size_t lane) const noexcept
{
  if (lane >= impl_->workers.size() || !impl_->workers[lane]) return {};
  const auto& timing = impl_->workers[lane]->timing;
  return {
    timing.calls.load(std::memory_order_relaxed),
    timing.totalNanoseconds.load(std::memory_order_relaxed),
    timing.maximumNanoseconds.load(std::memory_order_relaxed),
    timing.requestedCpu,
    timing.actualCpu.load(std::memory_order_relaxed),
  };
}

void WdwPairExecutor::reset() noexcept
{
  if (!impl_->configured) return;
#if defined(__linux__)
  if (impl_->options.mode == WdwPairExecutionMode::Pipelined) {
    const std::uint64_t wanted = impl_->submittedGeneration;
    for (const auto& worker : impl_->workers) {
      while (worker->completed.load(std::memory_order_acquire) < wanted) {
        std::this_thread::yield();
      }
    }
  }
#endif
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->outputAgeBlocks = 0;
  impl_->underflows = 0;
  impl_->submissionMisses = 0;
  impl_->haveLastPair = false;
  for (std::size_t slot = 0; slot < impl_->slotCount; ++slot) {
    auto& current = impl_->slots[slot];
    current.generation.store(0, std::memory_order_relaxed);
    current.completionMask.store(0, std::memory_order_relaxed);
    current.consumedGeneration.store(0, std::memory_order_relaxed);
    std::fill(current.input.begin(), current.input.end(), 0.0f);
    std::fill(current.dryLeft.begin(), current.dryLeft.end(), 0.0f);
    std::fill(current.dryRight.begin(), current.dryRight.end(), 0.0f);
    std::fill(current.wetLeft.begin(), current.wetLeft.end(), 0.0f);
    std::fill(current.wetRight.begin(), current.wetRight.end(), 0.0f);
  }
  std::fill(impl_->lastDryLeft.begin(), impl_->lastDryLeft.end(), 0.0f);
  std::fill(impl_->lastDryRight.begin(), impl_->lastDryRight.end(), 0.0f);
  std::fill(impl_->lastWetLeft.begin(), impl_->lastWetLeft.end(), 0.0f);
  std::fill(impl_->lastWetRight.begin(), impl_->lastWetRight.end(), 0.0f);
#if defined(__linux__)
  for (const auto& worker : impl_->workers) {
    worker->completed.store(0, std::memory_order_relaxed);
    worker->timing.calls.store(0, std::memory_order_relaxed);
    worker->timing.totalNanoseconds.store(0, std::memory_order_relaxed);
    worker->timing.maximumNanoseconds.store(0, std::memory_order_relaxed);
    worker->timing.actualCpu.store(-1, std::memory_order_relaxed);
  }
#else
  for (const auto& worker : impl_->workers) {
    worker->timing.calls.store(0, std::memory_order_relaxed);
    worker->timing.totalNanoseconds.store(0, std::memory_order_relaxed);
    worker->timing.maximumNanoseconds.store(0, std::memory_order_relaxed);
    worker->timing.actualCpu.store(-1, std::memory_order_relaxed);
  }
#endif
}

void WdwPairExecutor::clear() noexcept
{
  impl_->shutdown();
  impl_->options = {};
}

} // namespace ardor
