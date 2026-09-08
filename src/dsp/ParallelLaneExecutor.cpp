#include "dsp/ParallelLaneExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <utility>

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

struct ParallelLaneExecutor::Impl {
  struct Slot {
    std::vector<float> input;
    std::vector<std::vector<float>> outputs;
    std::uint64_t generation = 0;
  };

  struct Worker {
    ParallelLane lane{};
    int requestedCpu = -1;
    std::size_t laneIndex = 0;

#if defined(__linux__)
    sem_t jobReady{};
    bool semaphoreInitialized = false;
    std::thread thread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> setupComplete{false};
    std::atomic<bool> setupSucceeded{false};
    std::atomic<std::uint64_t> submitted{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<const float*> input{nullptr};
    std::atomic<float*> output{nullptr};
    std::atomic<std::size_t> frames{0};
#endif
  };

  ParallelLaneExecutorOptions options{};
  std::vector<ParallelLane> lanes;
  std::vector<Slot> slots;
  struct PublishedSlot {
    std::vector<std::vector<float>> outputs;
  };

  // Pipelined workers publish directly into this second ring.  It has one
  // more slot than the input ring, so a worker cannot overwrite the published
  // generation that the callback is handing to the graph before the next
  // callback has had a chance to consume it.  This moves the worker-output
  // handoff copy off the realtime callback while retaining bounded ownership.
  std::vector<PublishedSlot> publishedSlots;
  std::vector<std::unique_ptr<Worker>> workers;
  std::unique_ptr<TimingCounter[]> timings;

  bool configured = false;
  bool parallel = false;
  bool ready = false;
  bool sequentialFallback = false;
  std::size_t outputSlot = 0;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
  std::uint64_t pipelineUnderflows = 0;
  std::uint64_t pipelineSubmissionMisses = 0;
  std::uint64_t directWaitsOverBudget = 0;
  std::atomic<std::uint64_t> directWaitTotalNanoseconds{0};
  std::atomic<std::uint64_t> directWaitMaximumNanoseconds{0};
  // Workers must not recycle a published block until the callback has
  // finished consuming the older generation that maps to that slot.
  std::atomic<std::uint64_t> publishedConsumedGeneration{0};

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
    workers.clear();
    timings.reset();
    publishedSlots.clear();
    configured = false;
    parallel = false;
    ready = false;
    sequentialFallback = false;
  }

  bool allWorkersCompleted(std::uint64_t generation) const noexcept
  {
    if (sequentialFallback) return true;
#if defined(__linux__)
    for (const auto& worker : workers) {
      if (worker->completed.load(std::memory_order_acquire) < generation) return false;
    }
#else
    (void)generation;
#endif
    return true;
  }

#if defined(__linux__)
  static bool configureWorkerScheduling(Worker& worker,
                                        const ParallelLaneExecutorOptions& options)
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
    // Match the existing realtime NAM workers: flush denormals in the worker
    // thread rather than paying for them in every DSP callback.
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (std::uint64_t{1} << 24);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    return success;
  }

  void workerMain(Worker& worker)
  {
    const bool setupSucceeded = configureWorkerScheduling(worker, options);
    worker.setupSucceeded.store(setupSucceeded, std::memory_order_relaxed);
    worker.setupComplete.store(true, std::memory_order_release);
    if (options.collectTiming && timings) {
      timings[worker.laneIndex].actualCpu.store(currentCpu(), std::memory_order_relaxed);
    }

    while (true) {
      while (sem_wait(&worker.jobReady) != 0 && errno == EINTR) {
      }
      if (worker.stopping.load(std::memory_order_acquire)) break;

      std::uint64_t generation = worker.submitted.load(std::memory_order_acquire);
      const float* input = worker.input.load(std::memory_order_acquire);
      float* output = worker.output.load(std::memory_order_acquire);
      std::size_t frames = worker.frames.load(std::memory_order_acquire);
      if (options.mode == ParallelLaneExecutionMode::Pipelined && !slots.empty()) {
        // One semaphore post represents one accepted generation. Consume the
        // next generation in ring order instead of reading a mutable latest
        // pointer, which could otherwise skip a block when two posts arrive
        // before this worker wakes.
        generation = worker.completed.load(std::memory_order_acquire) + 1;
        if (generation == 0) continue;
        Slot& slot = slots[static_cast<std::size_t>(generation % slots.size())];
        if (slot.generation != generation) continue;
        input = slot.input.data();
        if (!publishedSlots.empty()) {
          const std::uint64_t publicationSlots = publishedSlots.size();
          if (generation > publicationSlots) {
            const std::uint64_t required = generation - publicationSlots;
            while (publishedConsumedGeneration.load(std::memory_order_acquire)
                   < required) {
              if (worker.stopping.load(std::memory_order_acquire)) return;
              std::this_thread::yield();
            }
          }
          output = publishedSlots[static_cast<std::size_t>(generation
            % publishedSlots.size())].outputs[worker.laneIndex].data();
        } else {
          output = slot.outputs[worker.laneIndex].data();
        }
        frames = slot.input.size();
      }
      if (input && output && frames > 0 && worker.lane.process) {
        const auto start = options.collectTiming
          ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        worker.lane.process(worker.lane.context, input, output, frames);
        if (options.collectTiming && timings) {
          const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
          timings[worker.laneIndex].actualCpu.store(currentCpu(), std::memory_order_relaxed);
          recordTiming(timings[worker.laneIndex], static_cast<std::uint64_t>(elapsed));
        }
      }
      worker.completed.store(generation, std::memory_order_release);
    }
  }
#endif

  bool startWorkers(std::string& error)
  {
    workers.clear();
    for (std::size_t laneIndex = 1; laneIndex < lanes.size(); ++laneIndex) {
      auto worker = std::make_unique<Worker>();
      worker->lane = lanes[laneIndex];
      worker->requestedCpu = lanes[laneIndex].workerCpu;
      worker->laneIndex = laneIndex;

#if defined(__linux__)
      if (sem_init(&worker->jobReady, 0, 0) != 0) {
        error = "parallel lane executor could not initialize a worker semaphore";
        return false;
      }
      worker->semaphoreInitialized = true;
      Worker* raw = worker.get();
      raw->thread = std::thread([this, raw]() { workerMain(*raw); });
      workers.push_back(std::move(worker));
#else
      (void)worker;
      if (options.requireWorkerSetup) {
        error = "parallel lane executor workers are unavailable on this platform";
        return false;
      }
      sequentialFallback = true;
      ready = false;
      return true;
#endif
    }

#if defined(__linux__)
    for (const auto& worker : workers) {
      while (!worker->setupComplete.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
    }

    ready = true;
    for (const auto& worker : workers) {
      if (!worker->setupSucceeded.load(std::memory_order_relaxed)) ready = false;
    }
#else
    ready = workers.empty();
#endif

    if (!ready) {
      if (options.requireWorkerSetup) {
        error = "parallel lane worker could not acquire requested realtime scheduling/affinity";
        return false;
      }
      sequentialFallback = true;
      parallel = false;
    } else {
      parallel = !workers.empty();
    }
    return true;
  }

  void dispatch(Slot& slot, std::uint64_t generation)
  {
#if defined(__linux__)
    for (std::size_t workerIndex = 0; workerIndex < workers.size(); ++workerIndex) {
      auto& worker = workers[workerIndex];
      worker->input.store(slot.input.data(), std::memory_order_relaxed);
      worker->output.store(slot.outputs[workerIndex + 1].data(), std::memory_order_relaxed);
      worker->frames.store(slot.input.size(), std::memory_order_relaxed);
      worker->submitted.store(generation, std::memory_order_release);
      sem_post(&worker->jobReady);
    }
#else
    (void)slot;
    (void)generation;
#endif
  }

  bool slotFree(const Slot& slot) const noexcept
  {
    return slot.generation == 0 || allWorkersCompleted(slot.generation);
  }

  void publishCompleted() noexcept
  {
    if (options.mode != ParallelLaneExecutionMode::Pipelined
        || sequentialFallback || slots.empty() || submittedGeneration == 0) {
      return;
    }

    // The ring may have advanced past an older generation before the callback
    // observes completion. Search the bounded live window from newest to
    // oldest and publish the newest complete slot that still exists. Workers
    // have already written the corresponding published slot before releasing
    // their completion counters, so no callback-side ring-to-snapshot copy is
    // needed here.
    const std::uint64_t firstLive = submittedGeneration >= slots.size()
      ? submittedGeneration - slots.size() + 1 : 1;
    const std::uint64_t lower = std::max(firstLive, outputGeneration + 1);
    for (std::uint64_t generation = submittedGeneration;
         generation >= lower; --generation) {
      const std::size_t slotIndex = static_cast<std::size_t>(
        generation % slots.size());
      const Slot& slot = slots[slotIndex];
      if (slot.generation != generation || !allWorkersCompleted(generation)) {
        continue;
      }
      auto& published = publishedSlots[static_cast<std::size_t>(generation
        % publishedSlots.size())];
      // Lane zero is callback-owned, so only this one lane still needs a
      // publication copy. Worker lanes were written directly into `published`
      // before releasing their completion counters.
      std::copy(slot.outputs[0].begin(), slot.outputs[0].end(),
                published.outputs[0].begin());
      outputSlot = slotIndex;
      outputGeneration = generation;
      break;
    }
  }

  bool processSequential(Slot& slot, const float* input, std::size_t frames,
                         ParallelLaneProcessResult& result)
  {
    std::copy(input, input + frames, slot.input.begin());
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
      const auto start = options.collectTiming
        ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      lanes[laneIndex].process(lanes[laneIndex].context, slot.input.data(),
                               slot.outputs[laneIndex].data(), frames);
      if (options.collectTiming && timings) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
        timings[laneIndex].actualCpu.store(currentCpu(), std::memory_order_relaxed);
        recordTiming(timings[laneIndex], static_cast<std::uint64_t>(elapsed));
      }
    }
    const std::uint64_t generation = ++submittedGeneration;
    slot.generation = generation;
    outputSlot = static_cast<std::size_t>(&slot - slots.data());
    outputGeneration = generation;
    result.accepted = true;
    result.outputReady = true;
    result.workersReady = ready;
    result.usedSequentialFallback = true;
    result.submittedGeneration = generation;
    result.outputGeneration = generation;
    return true;
  }

  bool processDirect(const float* input, std::size_t frames,
                     ParallelLaneProcessResult& result)
  {
    Slot& slot = slots.front();
    const auto start = std::chrono::steady_clock::now();
    std::copy(input, input + frames, slot.input.begin());
    const std::uint64_t generation = ++submittedGeneration;
    slot.generation = generation;
    dispatch(slot, generation);
    // Start the callback lane after the worker handoff so independent lanes
    // overlap for the whole block, matching the intended direct scheduler
    // topology.
    const auto laneStart = options.collectTiming
      ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    lanes[0].process(lanes[0].context, slot.input.data(), slot.outputs[0].data(), frames);
    if (options.collectTiming && timings) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - laneStart).count();
      timings[0].actualCpu.store(currentCpu(), std::memory_order_relaxed);
      recordTiming(timings[0], static_cast<std::uint64_t>(elapsed));
    }
    const auto waitStart = std::chrono::steady_clock::now();

    while (!allWorkersCompleted(generation)) {
      // Workers are pinned separately in the realtime configuration.  This is
      // intentionally a bounded handoff rather than a mutex/condition wait.
    }
    const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
    if (options.collectTiming) {
      const auto waitNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - waitStart).count());
      directWaitTotalNanoseconds.fetch_add(waitNanoseconds, std::memory_order_relaxed);
      auto maximum = directWaitMaximumNanoseconds.load(std::memory_order_relaxed);
      while (maximum < waitNanoseconds
             && !directWaitMaximumNanoseconds.compare_exchange_weak(
               maximum, waitNanoseconds, std::memory_order_relaxed)) {
      }
    }
    if (options.sampleRate > 0.0
        && elapsed > static_cast<double>(frames) / options.sampleRate) {
      ++directWaitsOverBudget;
    }

    outputSlot = 0;
    outputGeneration = generation;
    result.accepted = true;
    result.outputReady = true;
    result.workersReady = ready;
    result.submittedGeneration = generation;
    result.outputGeneration = generation;
    return true;
  }

  bool processPipelined(const float* input, std::size_t frames,
                        ParallelLaneProcessResult& result)
  {
    // Publish any completed output before writing a new generation into the
    // ring. This ordering is the ownership boundary for worker output.
    publishCompleted();

    const std::size_t slotIndex = static_cast<std::size_t>(
      (submittedGeneration + 1) % slots.size());
    Slot& slot = slots[slotIndex];
    if (!slotFree(slot)) {
      ++pipelineSubmissionMisses;
      result.workersReady = ready;
      result.outputReady = false;
      if (outputGeneration != 0 && allWorkersCompleted(outputGeneration)) {
        result.outputReady = true;
        result.outputGeneration = outputGeneration;
      } else if (outputGeneration == 0) {
        ++pipelineUnderflows;
      }
      return true;
    }

    std::copy(input, input + frames, slot.input.begin());
    const std::uint64_t generation = ++submittedGeneration;
    slot.generation = generation;
    dispatch(slot, generation);
    // The callback lane and worker lanes are independent.  Dispatch first so
    // the worker has the full block quantum to finish before the next output
    // handoff is attempted.
    const auto laneStart = options.collectTiming
      ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    lanes[0].process(lanes[0].context, slot.input.data(), slot.outputs[0].data(), frames);
    if (options.collectTiming && timings) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - laneStart).count();
      timings[0].actualCpu.store(currentCpu(), std::memory_order_relaxed);
      recordTiming(timings[0], static_cast<std::uint64_t>(elapsed));
    }

    result.accepted = true;
    result.workersReady = ready;
    result.submittedGeneration = generation;
    if (outputGeneration != 0) {
      result.outputReady = true;
      result.outputGeneration = outputGeneration;
    } else {
      ++pipelineUnderflows;
    }
    return true;
  }

  void acknowledgeOutput() noexcept
  {
    if (options.mode == ParallelLaneExecutionMode::Pipelined
        && !sequentialFallback && outputGeneration != 0) {
      // The graph mixer calls this only after it has copied every lane into
      // its own accumulation buffers. Until then workers must not recycle the
      // corresponding publication slot.
      publishedConsumedGeneration.store(outputGeneration, std::memory_order_release);
    }
  }

  const float* output(std::size_t lane) const noexcept
  {
    if (lane >= lanes.size() || outputGeneration == 0) {
      return nullptr;
    }
    if (options.mode == ParallelLaneExecutionMode::Pipelined && !sequentialFallback) {
      if (publishedSlots.empty()) return nullptr;
      return publishedSlots[static_cast<std::size_t>(outputGeneration
        % publishedSlots.size())].outputs[lane].data();
    }
    if (outputSlot >= slots.size()) return nullptr;
    return slots[outputSlot].outputs[lane].data();
  }
};

ParallelLaneExecutor::ParallelLaneExecutor()
  : impl_(std::make_unique<Impl>())
{
}

ParallelLaneExecutor::~ParallelLaneExecutor() = default;

bool ParallelLaneExecutor::configure(std::vector<ParallelLane> lanes,
                                     ParallelLaneExecutorOptions options,
                                     std::string& error)
{
  error.clear();
  impl_->shutdown();
  impl_->options = options;
  impl_->lanes = std::move(lanes);
  impl_->slots.clear();
  impl_->outputSlot = 0;
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->pipelineUnderflows = 0;
  impl_->pipelineSubmissionMisses = 0;
  impl_->directWaitsOverBudget = 0;
  impl_->directWaitTotalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->directWaitMaximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);

  if (impl_->lanes.empty()) {
    error = "parallel lane executor requires at least one lane";
    return false;
  }
  if (options.blockSize == 0 || !std::isfinite(options.sampleRate)
      || options.sampleRate <= 0.0) {
    error = "parallel lane executor requires a valid block size and sample rate";
    return false;
  }
  if (options.mode == ParallelLaneExecutionMode::Pipelined
      && options.pipelineSlots < 2) {
    error = "pipelined lane executor requires at least two slots";
    return false;
  }
  for (const auto& lane : impl_->lanes) {
    if (!lane.process) {
      error = "parallel lane executor received a lane without a process callback";
      return false;
    }
  }

  impl_->timings = std::make_unique<TimingCounter[]>(impl_->lanes.size());
  for (std::size_t lane = 0; lane < impl_->lanes.size(); ++lane) {
    impl_->timings[lane].requestedCpu = impl_->lanes[lane].workerCpu;
  }

  const std::size_t slotCount = options.mode == ParallelLaneExecutionMode::Pipelined
    ? options.pipelineSlots : 1;
  impl_->slots.resize(slotCount);
  for (auto& slot : impl_->slots) {
    slot.input.assign(options.blockSize, 0.0f);
    slot.outputs.resize(impl_->lanes.size());
    for (auto& output : slot.outputs) output.assign(options.blockSize, 0.0f);
  }
  const std::size_t publishedSlotCount = options.mode
    == ParallelLaneExecutionMode::Pipelined ? options.pipelineSlots + 1 : 1;
  impl_->publishedSlots.resize(publishedSlotCount);
  for (auto& published : impl_->publishedSlots) {
    published.outputs.resize(impl_->lanes.size());
    for (auto& output : published.outputs) {
      output.assign(options.blockSize, 0.0f);
    }
  }

  if (!impl_->startWorkers(error)) {
    impl_->shutdown();
    return false;
  }
  impl_->configured = true;
  return true;
}

bool ParallelLaneExecutor::processBlock(const float* input, std::size_t frames,
                                        ParallelLaneProcessResult& result)
{
  result = {};
  if (!impl_->configured || !input || frames != impl_->options.blockSize) return false;
  result.workersReady = impl_->ready;

  if (impl_->sequentialFallback) {
    return impl_->processSequential(impl_->slots.front(), input, frames, result);
  }
  if (impl_->options.mode == ParallelLaneExecutionMode::Pipelined) {
    return impl_->processPipelined(input, frames, result);
  }
  return impl_->processDirect(input, frames, result);
}

const float* ParallelLaneExecutor::laneOutput(std::size_t lane) const noexcept
{
  return impl_->output(lane);
}

void ParallelLaneExecutor::acknowledgeOutput() noexcept
{
  impl_->acknowledgeOutput();
}

std::size_t ParallelLaneExecutor::laneCount() const noexcept
{
  return impl_->lanes.size();
}

std::size_t ParallelLaneExecutor::blockSize() const noexcept
{
  return impl_->options.blockSize;
}

bool ParallelLaneExecutor::parallelEnabled() const noexcept
{
  return impl_->parallel;
}

bool ParallelLaneExecutor::workersReady() const noexcept
{
  return impl_->ready;
}

std::uint64_t ParallelLaneExecutor::pipelineUnderflowCount() const noexcept
{
  return impl_->pipelineUnderflows;
}

std::uint64_t ParallelLaneExecutor::pipelineSubmissionMissCount() const noexcept
{
  return impl_->pipelineSubmissionMisses;
}

std::uint64_t ParallelLaneExecutor::directWaitOverBudgetCount() const noexcept
{
  return impl_->directWaitsOverBudget;
}

std::uint64_t ParallelLaneExecutor::directWaitNanoseconds() const noexcept
{
  return impl_->directWaitTotalNanoseconds.load(std::memory_order_relaxed);
}

std::uint64_t ParallelLaneExecutor::directWaitMaximumNanoseconds() const noexcept
{
  return impl_->directWaitMaximumNanoseconds.load(std::memory_order_relaxed);
}

ParallelLaneTimingSnapshot ParallelLaneExecutor::timing(std::size_t lane) const noexcept
{
  if (!impl_->timings || lane >= impl_->lanes.size()) return {};
  const auto& counter = impl_->timings[lane];
  return {
    counter.calls.load(std::memory_order_relaxed),
    counter.totalNanoseconds.load(std::memory_order_relaxed),
    counter.maximumNanoseconds.load(std::memory_order_relaxed),
    counter.requestedCpu,
    counter.actualCpu.load(std::memory_order_relaxed),
  };
}

void ParallelLaneExecutor::reset() noexcept
{
  if (!impl_->configured) return;
  // Reset invalidates all callback-visible outputs. Treat every submitted
  // generation as consumed before waiting for workers that may be parked on
  // the publication-ring ownership boundary.
  impl_->publishedConsumedGeneration.store(impl_->submittedGeneration,
                                            std::memory_order_release);
  for (const auto& slot : impl_->slots) {
    if (slot.generation == 0) continue;
    while (!impl_->allWorkersCompleted(slot.generation)) {
      std::this_thread::yield();
    }
  }
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->outputSlot = 0;
  impl_->pipelineUnderflows = 0;
  impl_->pipelineSubmissionMisses = 0;
  impl_->directWaitsOverBudget = 0;
  impl_->directWaitTotalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->directWaitMaximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);
  for (auto& slot : impl_->slots) {
    slot.generation = 0;
    std::fill(slot.input.begin(), slot.input.end(), 0.0f);
    for (auto& output : slot.outputs) std::fill(output.begin(), output.end(), 0.0f);
  }
  for (auto& published : impl_->publishedSlots) {
    for (auto& output : published.outputs) {
      std::fill(output.begin(), output.end(), 0.0f);
    }
  }
#if defined(__linux__)
  for (auto& worker : impl_->workers) {
    worker->submitted.store(0, std::memory_order_relaxed);
    worker->completed.store(0, std::memory_order_relaxed);
  }
#endif
  if (impl_->timings) {
    for (std::size_t lane = 0; lane < impl_->lanes.size(); ++lane) {
      impl_->timings[lane].calls.store(0, std::memory_order_relaxed);
      impl_->timings[lane].totalNanoseconds.store(0, std::memory_order_relaxed);
      impl_->timings[lane].maximumNanoseconds.store(0, std::memory_order_relaxed);
      impl_->timings[lane].actualCpu.store(-1, std::memory_order_relaxed);
    }
  }
}

void ParallelLaneExecutor::clear() noexcept
{
  impl_->shutdown();
  impl_->options = {};
  impl_->lanes.clear();
  impl_->slots.clear();
  impl_->publishedSlots.clear();
  impl_->outputSlot = 0;
  impl_->submittedGeneration = 0;
  impl_->outputGeneration = 0;
  impl_->pipelineUnderflows = 0;
  impl_->pipelineSubmissionMisses = 0;
  impl_->directWaitsOverBudget = 0;
  impl_->directWaitTotalNanoseconds.store(0, std::memory_order_relaxed);
  impl_->directWaitMaximumNanoseconds.store(0, std::memory_order_relaxed);
  impl_->publishedConsumedGeneration.store(0, std::memory_order_relaxed);
}

} // namespace ardor
