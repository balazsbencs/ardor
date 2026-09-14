// Target-hardware feasibility probe for a future flexible routing graph.
//
// This is deliberately a benchmark rather than an audio callback. It exercises
// the existing RuntimeChain and the prepared flexible-routing owner with real
// NAM models, then measures the scheduling shape we need for more than two
// parallel lanes. UI and preset loading remain outside this probe.

#include "dsp/DenormalGuard.h"
#include "dsp/DualRigProcessor.h"
#include "dsp/FlexibleRoutingGraph.h"
#include "dsp/FlexibleRoutingProgram.h"
#include "dsp/ParallelLaneExecutor.h"
#include "dsp/ParallelLaneMixer.h"
#include "dsp/RuntimeChain.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <semaphore.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kCabImpulseFrames = 8192;

struct Options {
  std::vector<std::filesystem::path> models;
  std::vector<std::size_t> blockSizes{32, 64, 128};
  std::vector<int> workerCpus{0, 1, 3};
  std::size_t warmupBlocks = 100;
  std::size_t timedBlocks = 1000;
  int audioCpu = 2;
  bool includeCab = true;
  bool pace = false;
  bool onlyPostPipeline = false;
  bool emitTelemetry = false;
};

struct Stats {
  double minimumUs = 0.0;
  double meanUs = 0.0;
  double p99Us = 0.0;
  double p999Us = 0.0;
  double maximumUs = 0.0;
  std::size_t deadlineMisses = 0;
  double checksum = 0.0;
};

struct Row {
  std::string scenario;
  std::size_t blockSize = 0;
  std::size_t laneCount = 0;
  std::size_t namCount = 0;
  bool hasCab = false;
  std::size_t workers = 0;
  bool workersReady = true;
  bool pipelined = false;
  std::uint64_t pipelineUnderflows = 0;
  std::uint64_t pipelineSubmissionMisses = 0;
  std::uint64_t postJoinUnderflows = 0;
  std::uint64_t postJoinSubmissionMisses = 0;
  double aggregateFactor = 1.0;
  Stats stats;
};

void usage(const char* program)
{
  std::cerr
    << "Usage: " << program << " [options] MODEL...\n"
    << "Options:\n"
    << "  --block-sizes N,N,...  Frame quanta (default: 32,64,128)\n"
    << "  --warmup N             Untimed blocks per case (default: 100)\n"
    << "  --iterations N         Timed blocks per case (default: 1000)\n"
    << "  --audio-cpu N          Main/callback CPU (default: 2)\n"
    << "  --worker-cpus N,N,...  Worker CPUs (default: 0,1,3)\n"
    << "  --no-cab               Omit the synthetic 8192-sample cabinet IR\n"
    << "  --pace                 Sleep between blocks at the requested audio quantum\n"
    << "  --only-post-pipeline  Measure only the prepared shared-cab post pipeline\n"
    << "  --telemetry            Emit worker/handoff timing diagnostics on stderr\n"
    << "  -h, --help             Show this help\n";
}

bool parsePositiveSize(std::string_view text, std::size_t& output)
{
  if (text.empty()) return false;
  std::size_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (value > (static_cast<std::size_t>(-1) - digit) / 10) return false;
    value = value * 10 + digit;
  }
  if (value == 0) return false;
  output = value;
  return true;
}

bool parseCpu(std::string_view text, int& output)
{
  std::size_t value = 0;
  if (text == "0") {
    output = 0;
    return true;
  }
  if (!parsePositiveSize(text, value) || value > static_cast<std::size_t>(INT32_MAX)) {
    return false;
  }
  output = static_cast<int>(value);
  return true;
}

template <typename T>
bool parseList(std::string_view text, std::vector<T>& output,
               bool (*parseOne)(std::string_view, T&))
{
  output.clear();
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t comma = text.find(',', begin);
    const auto token = text.substr(begin, comma == std::string_view::npos
                                            ? text.size() - begin
                                            : comma - begin);
    T value{};
    if (!parseOne(token, value)) return false;
    output.push_back(value);
    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }
  return !output.empty();
}

bool parseSizeList(std::string_view text, std::vector<std::size_t>& output)
{
  if (!parseList<std::size_t>(text, output, parsePositiveSize)) return false;
  for (const std::size_t value : output) {
    if (value > 4096) return false;
  }
  std::sort(output.begin(), output.end());
  output.erase(std::unique(output.begin(), output.end()), output.end());
  return true;
}

bool parseCpuList(std::string_view text, std::vector<int>& output)
{
  return parseList<int>(text, output, parseCpu);
}

bool parseArgs(int argc, char** argv, Options& options)
{
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    auto next = [&]() -> const char* {
      return i + 1 < argc ? argv[++i] : nullptr;
    };
    if (argument == "-h" || argument == "--help") {
      usage(argv[0]);
      std::exit(0);
    }
    if (argument == "--block-sizes") {
      const char* value = next();
      if (!value || !parseSizeList(value, options.blockSizes)) return false;
    } else if (argument == "--warmup") {
      const char* value = next();
      if (!value || !parsePositiveSize(value, options.warmupBlocks)) return false;
    } else if (argument == "--iterations") {
      const char* value = next();
      if (!value || !parsePositiveSize(value, options.timedBlocks)) return false;
    } else if (argument == "--audio-cpu") {
      const char* value = next();
      if (!value || !parseCpu(value, options.audioCpu)) return false;
    } else if (argument == "--worker-cpus") {
      const char* value = next();
      if (!value || !parseCpuList(value, options.workerCpus)) return false;
    } else if (argument == "--no-cab") {
      options.includeCab = false;
    } else if (argument == "--pace") {
      options.pace = true;
    } else if (argument == "--only-post-pipeline") {
      options.onlyPostPipeline = true;
    } else if (argument == "--telemetry") {
      options.emitTelemetry = true;
    } else if (!argument.empty() && argument.front() == '-') {
      return false;
    } else {
      options.models.emplace_back(argument);
    }
  }
  return !options.models.empty();
}

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

bool pinCurrentThread(int cpu)
{
#if defined(__linux__)
  if (cpu < 0 || cpu >= CPU_SETSIZE) return false;
  cpu_set_t cpus;
  CPU_ZERO(&cpus);
  CPU_SET(cpu, &cpus);
  return pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) == 0;
#else
  (void)cpu;
  return false;
#endif
}

bool setRealtimePriority(int priority)
{
#if defined(__linux__)
  sched_param requested{};
  requested.sched_priority = priority;
  return pthread_setschedparam(pthread_self(), SCHED_FIFO, &requested) == 0;
#else
  (void)priority;
  return false;
#endif
}

std::vector<float> makeCabImpulse()
{
  std::vector<float> impulse(kCabImpulseFrames, 0.0f);
  // A deterministic, decaying, non-sparse impulse keeps the FFT partition
  // work representative without needing a user asset in the source tree.
  for (std::size_t i = 0; i < impulse.size(); ++i) {
    const float decay = std::exp(-static_cast<float>(i) / 1600.0f);
    const float ripple = 0.72f + 0.28f * std::cos(static_cast<float>(i) * 0.013f);
    impulse[i] = decay * ripple * (i == 0 ? 0.9f : 0.035f);
  }
  return impulse;
}

std::unique_ptr<ardor::RuntimeChain> makeChain(const Options& options,
                                                std::size_t blockSize,
                                                std::size_t namCount,
                                                bool includeCab,
                                                std::size_t modelOffset = 0)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  const auto impulse = includeCab ? makeCabImpulse() : std::vector<float>{};
  for (std::size_t i = 0; i < namCount; ++i) {
    const auto& model = options.models[(modelOffset + i) % options.models.size()];
    require(chain->addNam(model, kSampleRate, static_cast<int>(blockSize), "nam-" + std::to_string(i)),
            "failed to load NAM model: " + model.string());
  }
  if (includeCab) chain->addCab(impulse, 1.0f, 1.0f, "cab");
  chain->prepareBlockSize(blockSize);
  return chain;
}

std::vector<float> makeInput(std::size_t frames)
{
  std::vector<float> input(frames);
  uint32_t state = 0x6d2b79f5u;
  for (float& sample : input) {
    state = state * 1664525u + 1013904223u;
    sample = (static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f) * 0.25f;
  }
  return input;
}

double percentile(std::vector<double> samples, double fraction)
{
  if (samples.empty()) return 0.0;
  std::sort(samples.begin(), samples.end());
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(samples.size() - 1)));
  return samples[index];
}

template <typename Process>
Stats measure(const Options& options, std::size_t frames, Process&& process)
{
  ardor::ScopedDenormalGuard denormals;
  std::vector<double> samples;
  samples.reserve(options.timedBlocks);
  const double budgetUs = static_cast<double>(frames) / kSampleRate * 1.0e6;
  double sum = 0.0;
  double checksum = 0.0;
  std::size_t misses = 0;
  const auto blockDuration = std::chrono::duration<double>(
    static_cast<double>(frames) / kSampleRate);
  auto nextBlock = std::chrono::steady_clock::now();
  auto paceBlock = [&]() {
    if (!options.pace) return;
    nextBlock += std::chrono::duration_cast<std::chrono::steady_clock::duration>(blockDuration);
    std::this_thread::sleep_until(nextBlock);
    if (std::chrono::steady_clock::now() > nextBlock + blockDuration) {
      nextBlock = std::chrono::steady_clock::now();
    }
  };

  for (std::size_t i = 0; i < options.warmupBlocks; ++i) {
    process(false, checksum);
    paceBlock();
  }
  nextBlock = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < options.timedBlocks; ++i) {
    const auto start = std::chrono::steady_clock::now();
    process(true, checksum);
    const double elapsed = std::chrono::duration<double, std::micro>(
      std::chrono::steady_clock::now() - start).count();
    samples.push_back(elapsed);
    sum += elapsed;
    if (elapsed > budgetUs) ++misses;
    paceBlock();
  }

  Stats result;
  result.minimumUs = *std::min_element(samples.begin(), samples.end());
  result.meanUs = sum / static_cast<double>(samples.size());
  result.p99Us = percentile(samples, 0.99);
  result.p999Us = percentile(samples, 0.999);
  result.maximumUs = *std::max_element(samples.begin(), samples.end());
  result.deadlineMisses = misses;
  result.checksum = checksum;
  return result;
}

Row measureSerial(const Options& options, std::size_t blockSize,
                  std::size_t namCount, bool includeCab)
{
  auto chain = makeChain(options, blockSize, namCount, includeCab);
  const auto input = makeInput(blockSize);
  std::vector<float> left(blockSize, 0.0f);
  std::vector<float> right(blockSize, 0.0f);
  Row row;
  row.scenario = "serial-" + std::to_string(namCount) + "nam" + (includeCab ? "-cab" : "");
  row.blockSize = blockSize;
  row.namCount = namCount;
  row.hasCab = includeCab;
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    chain->processBlock(input.data(), left.data(), right.data(), blockSize);
    if (timed) checksum += static_cast<double>(left.front()) + right.back();
  });
  return row;
}

Row measureDualRig(const Options& options, std::size_t blockSize, bool requestParallel)
{
  ardor::DualRigLaneConfig left;
  ardor::DualRigLaneConfig right;
  left.chain = makeChain(options, blockSize, 1, options.includeCab);
  right.chain = makeChain(options, blockSize, 1, options.includeCab, 1);
  std::string error;
  ardor::DualRigProcessor rig;
  require(rig.configure(std::move(left), std::move(right), ardor::NamInputMode::Sum,
                        kSampleRate, blockSize, requestParallel,
                        options.workerCpus.empty() ? -1 : options.workerCpus.back(), error),
          "failed to configure Dual Rig: " + error);
  const auto input = makeInput(blockSize);
  std::vector<float> outputLeft(blockSize, 0.0f);
  std::vector<float> outputRight(blockSize, 0.0f);
  Row row;
  row.scenario = requestParallel ? "dualrig-par-2x-nam" : "dualrig-seq-2x-nam";
  row.scenario += options.includeCab ? "-cab" : "";
  row.blockSize = blockSize;
  row.laneCount = 2;
  row.namCount = 2;
  row.hasCab = options.includeCab;
  row.workers = requestParallel ? 1 : 0;
  row.aggregateFactor = requestParallel ? 2.0 : 1.0;
  row.workersReady = !requestParallel || rig.parallelEnabled();
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    rig.processBlock(input.data(), input.data(), outputLeft.data(), outputRight.data(), blockSize);
    if (timed) checksum += static_cast<double>(outputLeft.front()) + outputRight.back();
  });
  row.workersReady = !requestParallel || rig.parallelEnabled();
  return row;
}

class ParallelLaneHarness {
public:
  ParallelLaneHarness(const Options& options, std::size_t blockSize,
                      std::size_t laneCount, bool sharedCab)
  {
    require(laneCount >= 2, "parallel lane harness requires at least two lanes");
    require(options.workerCpus.size() >= laneCount - 1,
            "not enough worker CPUs for requested lane count");
    input_.resize(blockSize);
    if (sharedCab) {
      sharedCab_ = makeChain(options, blockSize, 0, true);
      mixed_.assign(blockSize, 0.0f);
      sharedOutputLeft_.assign(blockSize, 0.0f);
      sharedOutputRight_.assign(blockSize, 0.0f);
    }
    lanes_.reserve(laneCount);
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      auto task = std::make_unique<Task>();
      task->chain = makeChain(options, blockSize, 1,
                              options.includeCab && !sharedCab, lane);
      task->outputLeft.assign(blockSize, 0.0f);
      task->outputRight.assign(blockSize, 0.0f);
      if (lane > 0) {
        task->worker = true;
        task->cpu = options.workerCpus[lane - 1];
        require(sem_init(&task->ready, 0, 0) == 0, "sem_init failed");
        Task* raw = task.get();
        task->thread = std::thread([this, raw] { workerLoop(*raw); });
        while (!raw->setupComplete.load(std::memory_order_acquire)) std::this_thread::yield();
      }
      lanes_.push_back(std::move(task));
    }
  }

  ~ParallelLaneHarness()
  {
    for (auto& task : lanes_) {
      if (!task->worker) continue;
      task->stopping.store(true, std::memory_order_release);
      sem_post(&task->ready);
    }
    for (auto& task : lanes_) {
      if (!task->worker) continue;
      if (task->thread.joinable()) task->thread.join();
      sem_destroy(&task->ready);
    }
  }

  ParallelLaneHarness(const ParallelLaneHarness&) = delete;
  ParallelLaneHarness& operator=(const ParallelLaneHarness&) = delete;

  bool workersReady() const noexcept
  {
    for (std::size_t lane = 1; lane < lanes_.size(); ++lane) {
      if (!lanes_[lane]->setupSucceeded.load(std::memory_order_relaxed)) return false;
    }
    return true;
  }

  void process(const std::vector<float>& input, std::size_t frames,
               double& checksum, bool timed)
  {
    std::copy(input.begin(), input.end(), input_.begin());
    const uint64_t generation = ++generation_;
    for (std::size_t lane = 1; lane < lanes_.size(); ++lane) {
      auto& task = *lanes_[lane];
      task.input.store(input_.data(), std::memory_order_relaxed);
      task.frames.store(frames, std::memory_order_relaxed);
      task.generation.store(generation, std::memory_order_release);
      sem_post(&task.ready);
    }

    lanes_[0]->chain->processBlock(input_.data(), lanes_[0]->outputLeft.data(),
                                   lanes_[0]->outputRight.data(), frames);
    for (std::size_t lane = 1; lane < lanes_.size(); ++lane) {
      auto& task = *lanes_[lane];
      while (task.completed.load(std::memory_order_acquire) != generation) {
        // The callback is intentionally modeled as a bounded wait.  A future
        // pipeline can replace this with one-block look-ahead only after the
        // measurements show that direct waiting is insufficient.
      }
    }
    if (sharedCab_) {
      const float scale = 1.0f / static_cast<float>(lanes_.size());
      for (std::size_t i = 0; i < frames; ++i) {
        mixed_[i] = 0.0f;
        for (const auto& task : lanes_) mixed_[i] += task->outputLeft[i];
        mixed_[i] *= scale;
      }
      sharedCab_->processBlock(mixed_.data(), sharedOutputLeft_.data(),
                               sharedOutputRight_.data(), frames);
    }
    if (timed) {
      if (sharedCab_) {
        checksum += static_cast<double>(sharedOutputLeft_.front())
                    + sharedOutputRight_.back();
      } else {
        for (const auto& task : lanes_) {
          checksum += static_cast<double>(task->outputLeft.front())
                      + task->outputRight.back();
        }
      }
    }
  }

private:
  struct Task {
    std::unique_ptr<ardor::RuntimeChain> chain;
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    std::thread thread;
    sem_t ready{};
    std::atomic<bool> stopping{false};
    std::atomic<bool> setupComplete{false};
    std::atomic<bool> setupSucceeded{false};
    std::atomic<uint64_t> generation{0};
    std::atomic<uint64_t> completed{0};
    std::atomic<const float*> input{nullptr};
    std::atomic<std::size_t> frames{0};
    int cpu = -1;
    bool worker = false;
  };

  void workerLoop(Task& task)
  {
    bool setup = setRealtimePriority(69) && pinCurrentThread(task.cpu);
    task.setupSucceeded.store(setup, std::memory_order_relaxed);
    task.setupComplete.store(true, std::memory_order_release);
    ardor::ScopedDenormalGuard denormals;
    while (true) {
      while (sem_wait(&task.ready) != 0 && errno == EINTR) {
      }
      if (task.stopping.load(std::memory_order_acquire)) return;
      const uint64_t generation = task.generation.load(std::memory_order_acquire);
      const float* input = task.input.load(std::memory_order_relaxed);
      task.chain->processBlock(input, task.outputLeft.data(), task.outputRight.data(),
                               task.frames.load(std::memory_order_relaxed));
      task.completed.store(generation, std::memory_order_release);
    }
  }

  std::vector<float> input_;
  std::unique_ptr<ardor::RuntimeChain> sharedCab_;
  std::vector<float> mixed_;
  std::vector<float> sharedOutputLeft_;
  std::vector<float> sharedOutputRight_;
  std::vector<std::unique_ptr<Task>> lanes_;
  uint64_t generation_ = 0;
};

// Exercises the reusable executor that will back the future graph scheduler.
// The adapter deliberately selects RuntimeChain's first (mono) output and
// copies it into the executor-owned lane buffer; production integration can
// replace this with a zero-copy lane callback once the scheduler is admitted.
class ExecutorLaneHarness {
public:
  ExecutorLaneHarness(const Options& options, std::size_t blockSize,
                      std::size_t laneCount, bool sharedCab,
                      ardor::ParallelLaneExecutionMode mode,
                      bool stereoMix)
    : sharedCabEnabled_(sharedCab && options.includeCab), stereoMix_(stereoMix)
  {
    require(laneCount >= 2, "executor lane harness requires at least two lanes");
    require(options.workerCpus.size() >= laneCount - 1,
            "not enough worker CPUs for executor lane count");
    require(!sharedCabEnabled_ || !stereoMix_,
            "executor stereo mixer cannot feed the shared cab probe");
    if (sharedCabEnabled_) {
      sharedCab_ = makeChain(options, blockSize, 0, true);
      mixed_.assign(blockSize, 0.0f);
      sharedOutputLeft_.assign(blockSize, 0.0f);
      sharedOutputRight_.assign(blockSize, 0.0f);
    }

    tasks_.reserve(laneCount);
    std::vector<ardor::ParallelLane> lanes;
    lanes.reserve(laneCount);
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      auto task = std::make_unique<Task>();
      const bool laneCab = options.includeCab && !sharedCabEnabled_ && !stereoMix_;
      task->chain = makeChain(options, blockSize, 1,
                              laneCab, lane);
      task->first.assign(blockSize, 0.0f);
      task->second.assign(blockSize, 0.0f);
      tasks_.push_back(std::move(task));
    }
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      lanes.push_back({
        tasks_[lane].get(), &ExecutorLaneHarness::processTask,
        lane == 0 ? -1 : options.workerCpus[lane - 1],
      });
    }

    if (stereoMix_) {
      require(mixer_.prepare(laneCount, blockSize),
              "failed to prepare executor stereo mixer");
      mixerInputs_.assign(laneCount, nullptr);
      mixedLeft_.assign(blockSize, 0.0f);
      mixedRight_.assign(blockSize, 0.0f);
      for (std::size_t lane = 0; lane < laneCount; ++lane) {
        const float pan = laneCount == 2
          ? (lane == 0 ? -1.0f : 1.0f)
          : -1.0f + 2.0f * static_cast<float>(lane)
              / static_cast<float>(laneCount - 1);
        require(mixer_.setLane(lane, {1.0f, pan, true}),
                "failed to configure executor stereo pan");
      }
      mixer_.setDry({1.0f, 0.0f, true});
      mixer_.setUnderflowPolicy(ardor::ParallelLaneUnderflowPolicy::DryFallback);
    }

    ardor::ParallelLaneExecutorOptions executorOptions;
    executorOptions.blockSize = blockSize;
    executorOptions.sampleRate = kSampleRate;
    executorOptions.mode = mode;
    executorOptions.pipelineSlots = 2;
    executorOptions.workerPriority = 69;
    executorOptions.requireWorkerSetup = true;
    executorOptions.requireRealtimeScheduling = true;
    executorOptions.requireAffinity = true;
    std::string error;
    require(executor_.configure(std::move(lanes), executorOptions, error),
            "failed to configure reusable lane executor: " + error);
  }

  bool workersReady() const noexcept
  {
    return executor_.workersReady();
  }

  void process(const std::vector<float>& input, std::size_t frames,
               double& checksum, bool timed)
  {
    ardor::ParallelLaneProcessResult result;
    require(executor_.processBlock(input.data(), frames, result),
            "reusable lane executor rejected a valid block");
    if (stereoMix_) {
      for (std::size_t lane = 0; lane < tasks_.size(); ++lane) {
        mixerInputs_[lane] = result.outputReady ? executor_.laneOutput(lane) : nullptr;
      }
      require(mixer_.processBlock(input.data(), mixerInputs_.data(), result.outputReady,
                                  mixedLeft_.data(), mixedRight_.data(), frames),
              "executor stereo mixer rejected a valid block");
      if (!result.outputReady) ++outputMisses_;
      if (timed) {
        checksum += static_cast<double>(mixedLeft_.front()) + mixedRight_.back();
      }
      if (result.outputReady) executor_.acknowledgeOutput();
      return;
    }
    if (!result.outputReady) {
      ++outputMisses_;
      return;
    }

    if (sharedCabEnabled_) {
      const float scale = 1.0f / static_cast<float>(tasks_.size());
      for (std::size_t i = 0; i < frames; ++i) {
        float mixed = 0.0f;
        for (std::size_t lane = 0; lane < tasks_.size(); ++lane) {
          const float* output = executor_.laneOutput(lane);
          require(output != nullptr, "executor did not expose a ready lane output");
          mixed += output[i];
        }
        mixed_[i] = mixed * scale;
      }
      sharedCab_->processBlock(mixed_.data(), sharedOutputLeft_.data(),
                               sharedOutputRight_.data(), frames);
    }

    if (timed) {
      if (sharedCabEnabled_) {
        checksum += static_cast<double>(sharedOutputLeft_.front())
                    + sharedOutputRight_.back();
      } else {
        for (std::size_t lane = 0; lane < tasks_.size(); ++lane) {
          const float* output = executor_.laneOutput(lane);
          require(output != nullptr, "executor did not expose a ready lane output");
          checksum += static_cast<double>(output[0]) + output[frames - 1];
        }
      }
    }
    if (result.outputReady) executor_.acknowledgeOutput();
  }

  std::uint64_t outputMisses() const noexcept { return outputMisses_; }
  std::uint64_t submissionMisses() const noexcept
  {
    return executor_.pipelineSubmissionMissCount();
  }
  std::uint64_t underflows() const noexcept
  {
    if (stereoMix_) return mixer_.underflowBlockCount();
    // Report the externally visible misses.  The executor's internal
    // underflow counter describes the same event before this adapter decides
    // whether a previous output can still be consumed.
    return outputMisses_;
  }

private:
  struct Task {
    std::unique_ptr<ardor::RuntimeChain> chain;
    std::vector<float> first;
    std::vector<float> second;
  };

  static void processTask(void* opaque, const float* input, float* output,
                          std::size_t frames)
  {
    auto& task = *static_cast<Task*>(opaque);
    task.chain->processBlock(input, task.first.data(), task.second.data(), frames);
    std::copy(task.first.begin(), task.first.begin() + static_cast<std::ptrdiff_t>(frames),
              output);
  }

  bool sharedCabEnabled_ = false;
  std::unique_ptr<ardor::RuntimeChain> sharedCab_;
  std::vector<float> mixed_;
  std::vector<float> sharedOutputLeft_;
  std::vector<float> sharedOutputRight_;
  bool stereoMix_ = false;
  std::vector<const float*> mixerInputs_;
  std::vector<float> mixedLeft_;
  std::vector<float> mixedRight_;
  ardor::ParallelLaneMixer mixer_;
  std::vector<std::unique_ptr<Task>> tasks_;
  ardor::ParallelLaneExecutor executor_;
  std::uint64_t outputMisses_ = 0;
};

// Runs the same real RuntimeChain lane callbacks through the graph contract
// rather than wiring the executor and mixer together directly.  This keeps a
// graph boundary measured separately from the lower-level scheduler probe
// above.
class GraphLaneHarness {
public:
  GraphLaneHarness(const Options& options, std::size_t blockSize,
                   std::size_t laneCount,
                   ardor::ParallelLaneExecutionMode mode)
  {
    require(laneCount >= 2, "graph lane harness requires at least two lanes");
    require(options.workerCpus.size() >= laneCount - 1,
            "not enough worker CPUs for graph lane count");

    outputLeft_.assign(blockSize, 0.0f);
    outputRight_.assign(blockSize, 0.0f);

    tasks_.reserve(laneCount);
    std::vector<ardor::FlexibleRoutingLane> lanes;
    lanes.reserve(laneCount);
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      auto task = std::make_unique<Task>();
      // Keep this row focused on the lane graph and mixer. Cabinet placement
      // is measured separately because it remains the critical-path cost.
      task->chain = makeChain(options, blockSize, 1, false, lane);
      task->first.assign(blockSize, 0.0f);
      task->second.assign(blockSize, 0.0f);
      tasks_.push_back(std::move(task));
    }
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      lanes.push_back({
        "lane-" + std::to_string(lane), tasks_[lane].get(),
        &GraphLaneHarness::processTask,
        lane == 0 ? -1 : options.workerCpus[lane - 1],
        {1.0f, lane == 0 ? -1.0f : 1.0f, true},
      });
    }

    ardor::FlexibleRoutingGraphOptions graphOptions;
    graphOptions.executor.blockSize = blockSize;
    graphOptions.executor.sampleRate = kSampleRate;
    graphOptions.executor.mode = mode;
    graphOptions.executor.pipelineSlots = 2;
    graphOptions.executor.workerPriority = 69;
    graphOptions.executor.requireWorkerSetup = true;
    graphOptions.executor.requireRealtimeScheduling = true;
    graphOptions.executor.requireAffinity = true;
    graphOptions.dry = {1.0f, 0.0f, true};
    graphOptions.underflowPolicy = ardor::ParallelLaneUnderflowPolicy::DryFallback;

    std::vector<ardor::FlexibleRoutingJoinStage> joinStages{
      {"master", nullptr, &GraphLaneHarness::processJoin},
    };
    std::string error;
    require(graph_.configure(std::move(lanes), std::move(joinStages), graphOptions, error),
            "failed to configure flexible routing graph: " + error);
  }

  void process(const std::vector<float>& input, std::size_t frames,
               double& checksum, bool timed)
  {
    ardor::FlexibleRoutingGraphProcessResult result;
    require(graph_.processBlock(input.data(), outputLeft_.data(), outputRight_.data(),
                                frames, result),
            "flexible routing graph rejected a valid block");
    if (timed) {
      checksum += static_cast<double>(outputLeft_.front()) + outputRight_.back();
    }
  }

  bool workersReady() const noexcept { return graph_.workersReady(); }
  std::uint64_t underflows() const noexcept { return graph_.underflowBlockCount(); }
  std::uint64_t submissionMisses() const noexcept
  {
    return graph_.submissionMissCount();
  }

private:
  struct Task {
    std::unique_ptr<ardor::RuntimeChain> chain;
    std::vector<float> first;
    std::vector<float> second;
  };

  static void processTask(void* opaque, const float* input, float* output,
                          std::size_t frames)
  {
    auto& task = *static_cast<Task*>(opaque);
    task.chain->processBlock(input, task.first.data(), task.second.data(), frames);
    std::copy(task.first.begin(), task.first.begin() + static_cast<std::ptrdiff_t>(frames),
              output);
  }

  static void processJoin(void*, float*, float*, std::size_t) {}

  std::vector<std::unique_ptr<Task>> tasks_;
  std::vector<float> outputLeft_;
  std::vector<float> outputRight_;
  ardor::FlexibleRoutingGraph graph_;
};

// Measures the prepared RuntimeChain owner that PedalEngine can install. This
// intentionally uses real NAM chains, rather than synthetic callbacks, so the
// production ownership layer is timed separately from the generic graph API.
class ProgramLaneHarness {
public:
  ProgramLaneHarness(const Options& options, std::size_t blockSize,
                     std::size_t laneCount,
                     ardor::ParallelLaneExecutionMode mode,
                     bool sharedCab, bool postPipeline)
  {
    require(laneCount >= 2, "program lane harness requires at least two lanes");
    require(options.workerCpus.size() >= laneCount - 1 + (postPipeline ? 1 : 0),
            "not enough worker CPUs for program lane count");
    outputLeft_.assign(blockSize, 0.0f);
    outputRight_.assign(blockSize, 0.0f);

    std::vector<ardor::FlexibleRoutingProgramLane> lanes;
    lanes.reserve(laneCount);
    for (std::size_t lane = 0; lane < laneCount; ++lane) {
      lanes.push_back({
        "lane-" + std::to_string(lane),
        makeChain(options, blockSize, 1, false, lane),
        {1.0f, lane == 0 ? -1.0f : 1.0f, true},
        lane == 0 ? -1 : options.workerCpus[lane - 1],
        ardor::FlexibleRoutingLaneOutput::Downmix,
        false,
      });
    }

    std::vector<ardor::FlexibleRoutingProgramPostJoin> postJoins;
    if (sharedCab) {
      postJoins.push_back({"shared-cab", makeChain(options, blockSize, 0, true)});
    }

    ardor::FlexibleRoutingProgramOptions programOptions;
    programOptions.executor.blockSize = blockSize;
    programOptions.executor.sampleRate = kSampleRate;
    programOptions.executor.mode = mode;
    programOptions.executor.pipelineSlots = 2;
    programOptions.executor.workerPriority = 69;
    programOptions.executor.requireWorkerSetup = true;
    programOptions.executor.requireRealtimeScheduling = true;
    programOptions.executor.requireAffinity = true;
    programOptions.executor.collectTiming = options.emitTelemetry;
    programOptions.postJoinExecutor.mode = postPipeline
      ? ardor::ParallelStereoStageExecutionMode::Pipelined
      : ardor::ParallelStereoStageExecutionMode::Direct;
    programOptions.postJoinExecutor.pipelineSlots = 2;
    programOptions.postJoinExecutor.workerCpu = postPipeline
      ? options.workerCpus[laneCount - 1] : -1;
    programOptions.postJoinExecutor.workerPriority = 69;
    programOptions.postJoinExecutor.requireWorkerSetup = true;
    programOptions.postJoinExecutor.requireRealtimeScheduling = true;
    programOptions.postJoinExecutor.requireAffinity = true;
    programOptions.postJoinExecutor.collectTiming = options.emitTelemetry;
    programOptions.postJoinExecutor.underflowPolicy =
      ardor::ParallelStereoStageUnderflowPolicy::Bypass;
    programOptions.dry = {1.0f, 0.0f, true};
    programOptions.underflowPolicy = ardor::ParallelLaneUnderflowPolicy::DryFallback;
    std::string error;
    program_ = std::make_unique<ardor::FlexibleRoutingProgram>();
    require(program_->prepare(std::move(lanes), std::move(postJoins), programOptions, error),
            "failed to prepare flexible routing program: " + error);
  }

  void process(const std::vector<float>& input, std::size_t frames,
               double& checksum, bool timed)
  {
    ardor::FlexibleRoutingGraphProcessResult result;
    require(program_->processBlock(input.data(), outputLeft_.data(), outputRight_.data(),
                                   frames, result),
            "flexible routing program rejected a valid block");
    if (timed) {
      checksum += static_cast<double>(outputLeft_.front()) + outputRight_.back();
    }
  }

  bool workersReady() const noexcept { return program_->workersReady(); }
  std::uint64_t underflows() const noexcept
  {
    return program_->underflowBlockCount();
  }
  std::uint64_t submissionMisses() const noexcept
  {
    return program_->submissionMissCount();
  }
  std::uint64_t postJoinUnderflows() const noexcept
  {
    return program_->postJoinUnderflowBlockCount();
  }
  std::uint64_t postJoinSubmissionMisses() const noexcept
  {
    return program_->postJoinSubmissionMissCount();
  }

  void printTelemetry(std::string_view scenario) const
  {
    const auto printLane = [&](std::size_t lane) {
      const auto timing = program_->laneTiming(lane);
      const double meanUs = timing.calls == 0 ? 0.0
        : static_cast<double>(timing.totalNanoseconds)
            / static_cast<double>(timing.calls) / 1000.0;
      const double maxUs = static_cast<double>(timing.maximumNanoseconds) / 1000.0;
      std::cerr << " lane" << lane << "_calls=" << timing.calls
                << " lane" << lane << "_mean_us=" << std::fixed
                << std::setprecision(3) << meanUs
                << " lane" << lane << "_max_us=" << maxUs
                << " lane" << lane << "_requested_cpu=" << timing.requestedCpu
                << " lane" << lane << "_actual_cpu=" << timing.actualCpu;
    };
    const auto post = program_->postJoinTiming();
    const double postMeanUs = post.calls == 0 ? 0.0
      : static_cast<double>(post.totalNanoseconds)
          / static_cast<double>(post.calls) / 1000.0;
    const double postMaxUs = static_cast<double>(post.maximumNanoseconds) / 1000.0;
    const double waitMeanUs = program_->directWaitNanoseconds() == 0
      ? 0.0
      : static_cast<double>(program_->directWaitNanoseconds())
          / static_cast<double>(std::max<std::uint64_t>(1,
              program_->laneTiming(0).calls)) / 1000.0;
    std::cerr << "routing-telemetry scenario=" << scenario;
    for (std::size_t lane = 0; lane < program_->laneCount(); ++lane) {
      printLane(lane);
    }
    std::cerr << " post_calls=" << post.calls
              << " post_mean_us=" << std::fixed << std::setprecision(3)
              << postMeanUs << " post_max_us=" << postMaxUs
              << " post_requested_cpu=" << post.requestedCpu
              << " post_actual_cpu=" << post.actualCpu
              << " direct_wait_mean_us=" << waitMeanUs
              << " direct_wait_max_us=" << static_cast<double>(
                   program_->directWaitMaximumNanoseconds()) / 1000.0
              << " underflows=" << program_->underflowBlockCount()
              << " submissions=" << program_->submissionMissCount()
              << " post_underflows=" << program_->postJoinUnderflowBlockCount()
              << " post_submissions=" << program_->postJoinSubmissionMissCount()
              << "\n";
  }

private:
  std::unique_ptr<ardor::FlexibleRoutingProgram> program_;
  std::vector<float> outputLeft_;
  std::vector<float> outputRight_;
};

Row measureParallelLanes(const Options& options, std::size_t blockSize,
                         std::size_t laneCount, bool sharedCab = false)
{
  ParallelLaneHarness harness(options, blockSize, laneCount, sharedCab);
  const auto input = makeInput(blockSize);
  Row row;
  row.scenario = "lanes-par-" + std::to_string(laneCount) + "x-nam";
  if (options.includeCab) row.scenario += sharedCab ? "-shared-cab" : "-cab";
  row.blockSize = blockSize;
  row.laneCount = laneCount;
  row.namCount = laneCount;
  row.hasCab = options.includeCab;
  row.workers = laneCount - 1;
  row.aggregateFactor = static_cast<double>(laneCount) + (sharedCab ? 1.0 : 0.0);
  row.workersReady = harness.workersReady();
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    harness.process(input, blockSize, checksum, timed);
  });
  return row;
}

Row measureExecutorLanes(const Options& options, std::size_t blockSize,
                         std::size_t laneCount, bool sharedCab,
                         ardor::ParallelLaneExecutionMode mode,
                         bool stereoMix = false)
{
  ExecutorLaneHarness harness(options, blockSize, laneCount, sharedCab, mode,
                              stereoMix);
  const auto input = makeInput(blockSize);
  const bool pipelined = mode == ardor::ParallelLaneExecutionMode::Pipelined;
  Row row;
  row.scenario = std::string("executor-") + (pipelined ? "pipeline-" : "direct-")
               + std::to_string(laneCount) + "x-nam";
  if (stereoMix) {
    row.scenario += "-stereo-mix";
  } else if (options.includeCab) {
    row.scenario += sharedCab ? "-shared-cab" : "-cab";
  }
  row.blockSize = blockSize;
  row.laneCount = laneCount;
  row.namCount = laneCount;
  row.hasCab = options.includeCab && !stereoMix;
  row.workers = laneCount - 1;
  row.pipelined = pipelined;
  row.aggregateFactor = static_cast<double>(laneCount) + (sharedCab ? 1.0 : 0.0);
  row.workersReady = harness.workersReady();
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    harness.process(input, blockSize, checksum, timed);
  });
  row.workersReady = harness.workersReady();
  row.pipelineUnderflows = harness.underflows();
  row.pipelineSubmissionMisses = harness.submissionMisses();
  return row;
}

Row measureGraphLanes(const Options& options, std::size_t blockSize,
                      std::size_t laneCount,
                      ardor::ParallelLaneExecutionMode mode)
{
  GraphLaneHarness harness(options, blockSize, laneCount, mode);
  const auto input = makeInput(blockSize);
  Row row;
  const bool pipelined = mode == ardor::ParallelLaneExecutionMode::Pipelined;
  row.scenario = std::string("graph-") + (pipelined ? "pipeline-" : "direct-")
               + std::to_string(laneCount) + "x-nam-stereo-mix";
  row.blockSize = blockSize;
  row.laneCount = laneCount;
  row.namCount = laneCount;
  row.hasCab = false;
  row.workers = laneCount - 1;
  row.pipelined = pipelined;
  row.aggregateFactor = static_cast<double>(laneCount);
  row.workersReady = harness.workersReady();
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    harness.process(input, blockSize, checksum, timed);
  });
  row.workersReady = harness.workersReady();
  row.pipelineUnderflows = harness.underflows();
  row.pipelineSubmissionMisses = harness.submissionMisses();
  return row;
}

Row measureProgramLanes(const Options& options, std::size_t blockSize,
                        std::size_t laneCount,
                        ardor::ParallelLaneExecutionMode mode,
                        bool sharedCab = false, bool postPipeline = false)
{
  ProgramLaneHarness harness(options, blockSize, laneCount, mode, sharedCab,
                             postPipeline);
  const auto input = makeInput(blockSize);
  Row row;
  const bool pipelined = mode == ardor::ParallelLaneExecutionMode::Pipelined;
  row.scenario = std::string("program-") + (pipelined ? "pipeline-" : "direct-")
               + std::to_string(laneCount) + "x-nam-stereo-mix";
  if (sharedCab) row.scenario += postPipeline
    ? "-shared-cab-post-pipeline" : "-shared-cab";
  row.blockSize = blockSize;
  row.laneCount = laneCount;
  row.namCount = laneCount;
  row.hasCab = sharedCab;
  row.workers = laneCount - 1 + (postPipeline ? 1 : 0);
  row.pipelined = pipelined || postPipeline;
  row.aggregateFactor = static_cast<double>(laneCount) + (sharedCab ? 1.0 : 0.0);
  row.workersReady = harness.workersReady();
  row.stats = measure(options, blockSize, [&](bool timed, double& checksum) {
    harness.process(input, blockSize, checksum, timed);
  });
  row.workersReady = harness.workersReady();
  row.pipelineUnderflows = harness.underflows();
  row.pipelineSubmissionMisses = harness.submissionMisses();
  row.postJoinUnderflows = harness.postJoinUnderflows();
  row.postJoinSubmissionMisses = harness.postJoinSubmissionMisses();
  if (options.emitTelemetry) harness.printTelemetry(row.scenario);
  return row;
}

std::string csv(std::string_view value)
{
  std::string result{"\""};
  for (const char c : value) {
    if (c == '"') result += '"';
    result += c;
  }
  result += '"';
  return result;
}

void printHeader()
{
  std::cout << "scenario,block_size,lane_count,nam_count,has_cab,workers,workers_ready,"
               "pipelined,pipeline_underflows,pipeline_submission_misses,"
               "post_join_underflows,post_join_submission_misses,"
               "budget_us,min_us,mean_us,p99_us,p999_us,max_us,deadline_misses,"
               "aggregate_cpu_pct,checksum\n";
}

void printRow(const Row& row)
{
  const double budgetUs = static_cast<double>(row.blockSize) / kSampleRate * 1.0e6;
  const double aggregate = budgetUs > 0.0
    ? row.stats.meanUs * row.aggregateFactor
        / budgetUs * 100.0
    : 0.0;
  std::cout << csv(row.scenario) << ','
            << row.blockSize << ','
            << row.laneCount << ','
            << row.namCount << ','
            << (row.hasCab ? 1 : 0) << ','
            << row.workers << ','
            << (row.workersReady ? 1 : 0) << ','
            << (row.pipelined ? 1 : 0) << ','
            << row.pipelineUnderflows << ','
            << row.pipelineSubmissionMisses << ','
            << row.postJoinUnderflows << ','
            << row.postJoinSubmissionMisses << ','
            << std::fixed << std::setprecision(3)
            << budgetUs << ','
            << row.stats.minimumUs << ','
            << row.stats.meanUs << ','
            << row.stats.p99Us << ','
            << row.stats.p999Us << ','
            << row.stats.maximumUs << ','
            << row.stats.deadlineMisses << ','
            << aggregate << ','
            << row.stats.checksum << '\n';
}

} // namespace

int main(int argc, char** argv)
{
  Options options;
  if (!parseArgs(argc, argv, options)) {
    usage(argv[0]);
    return 2;
  }

  try {
    const bool pinned = pinCurrentThread(options.audioCpu);
    const bool realtime = setRealtimePriority(60);
    std::cerr << "routing-feasibility: audio_cpu=" << options.audioCpu
              << " pinned=" << (pinned ? 1 : 0)
              << " realtime=" << (realtime ? 1 : 0)
              << " models=" << options.models.size()
              << " cab=" << (options.includeCab ? 1 : 0) << "\n";
    printHeader();
    if (options.onlyPostPipeline) {
      if (!options.includeCab) {
        throw std::runtime_error("--only-post-pipeline requires the cabinet probe");
      }
      for (const std::size_t blockSize : options.blockSizes) {
        printRow(measureProgramLanes(options, blockSize, 2,
                                     ardor::ParallelLaneExecutionMode::Direct,
                                     true, true));
      }
      return 0;
    }
    for (const std::size_t blockSize : options.blockSizes) {
      printRow(measureSerial(options, blockSize, 1, false));
      printRow(measureSerial(options, blockSize, 2, false));
      if (options.includeCab) {
        printRow(measureSerial(options, blockSize, 2, true));
        printRow(measureSerial(options, blockSize, 3, true));
      } else {
        printRow(measureSerial(options, blockSize, 3, false));
      }
      printRow(measureDualRig(options, blockSize, false));
      printRow(measureDualRig(options, blockSize, true));
      printRow(measureParallelLanes(options, blockSize, 2));
      printRow(measureParallelLanes(options, blockSize, 3));
      if (options.includeCab) {
        printRow(measureParallelLanes(options, blockSize, 3, true));
      }
      // The reusable executor is reported separately from the original
      // benchmark-only worker harness above.  Pipeline rows expose both
      // latency and any bounded-ring underflow/submission misses.
      printRow(measureExecutorLanes(options, blockSize, 2, false,
                                    ardor::ParallelLaneExecutionMode::Direct));
      printRow(measureExecutorLanes(options, blockSize, 2, false,
                                    ardor::ParallelLaneExecutionMode::Pipelined));
      printRow(measureExecutorLanes(options, blockSize, 3, false,
                                    ardor::ParallelLaneExecutionMode::Pipelined));
      // Exercise the graph-side lane mixer with real NAM outputs: hard-left
      // and hard-right wet lanes plus a centre dry path.
      printRow(measureExecutorLanes(options, blockSize, 2, false,
                                    ardor::ParallelLaneExecutionMode::Pipelined,
                                    true));
      printRow(measureGraphLanes(options, blockSize, 2,
                                 ardor::ParallelLaneExecutionMode::Pipelined));
      printRow(measureProgramLanes(options, blockSize, 2,
                                   ardor::ParallelLaneExecutionMode::Pipelined));
      if (options.includeCab) {
        printRow(measureProgramLanes(options, blockSize, 2,
                                     ardor::ParallelLaneExecutionMode::Direct, true));
        printRow(measureProgramLanes(options, blockSize, 2,
                                     ardor::ParallelLaneExecutionMode::Direct, true,
                                     true));
      }
      if (options.includeCab) {
        printRow(measureExecutorLanes(options, blockSize, 2, true,
                                      ardor::ParallelLaneExecutionMode::Pipelined));
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "routing-feasibility: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
