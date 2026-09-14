// Private feasibility probe for the proposed two-lane wet/dry/wet topology.
//
// The probe deliberately keeps the live engine untouched.  It runs one
// complete dry lane and one complete wet lane through the pair-aware WDW
// program, which publishes only a generation-matched pair to the mixer:
//
//   dry: Rat (optional) -> NAM -> cab
//   wet: NAM -> cab -> stereo delay -> stereo reverb
//
// The first published result is silence rather than a raw-input bypass.  This
// is important for the proposed topology: there is no always-on direct path.
// By default the cab is a deterministic dense proxy; --cab-path exercises the
// same mono WAV validation path used by production loading.

#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DenormalGuard.h"
#include "dsp/RuntimeChain.h"
#include "dsp/WdwRoutingProgram.h"
#include "audio/WavIo.h"
#include "rat/RatProcessor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kCabImpulseFrames = 8192;

struct Options {
  std::vector<std::filesystem::path> models;
  std::vector<std::size_t> blockSizes{32, 64, 128};
  int audioCpu = 1;
  int dryWorkerCpu = 0;
  int wetWorkerCpu = 3;
  std::size_t warmupBlocks = 100;
  std::size_t timedBlocks = 1000;
  std::size_t pipelineSlots = 3;
  bool pace = false;
  bool includeRat = true;
  bool includeCab = true;
  std::filesystem::path cabPath;
  bool relaxedWorkers = false;
  bool onlyPipeline = false;
  bool onlyDirect = false;
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

struct LaneMetrics {
  std::uint64_t timingCalls = 0;
  std::uint64_t timingTotalNs = 0;
  std::uint64_t timingMaximumNs = 0;
  int requestedCpu = -1;
  int actualCpu = -1;
};

struct Row {
  std::string scenario;
  std::size_t blockSize = 0;
  bool pipelined = false;
  bool rat = false;
  bool cab = false;
  bool workersReady = false;
  std::uint64_t pairReady = 0;
  std::uint64_t pairUnderflows = 0;
  std::uint64_t pairSubmissionMisses = 0;
  std::size_t maxPairAgeBlocks = 0;
  std::uint64_t initialSilence = 0;
  std::uint64_t nonFiniteOutputs = 0;
  std::uint64_t dryFaultBlocks = 0;
  std::uint64_t wetFaultBlocks = 0;
  std::size_t dryLatencyFrames = 0;
  std::size_t wetLatencyFrames = 0;
  double wetStereoSpread = 0.0;
  LaneMetrics dry;
  LaneMetrics wet;
  Stats stats;
};

void usage(const char* program)
{
  std::cerr
    << "Usage: " << program << " [options] DRY_MODEL [WET_MODEL]\n"
    << "Options:\n"
    << "  --block-sizes N,N,...  Frame quanta (default: 32,64,128)\n"
    << "  --warmup N             Untimed blocks per case (default: 100)\n"
    << "  --iterations N         Timed blocks per case (default: 1000)\n"
    << "  --pipeline-slots N     Bounded pair ring slots (default: 3)\n"
    << "  --audio-cpu N          Main/callback CPU (default: 1)\n"
    << "  --dry-worker-cpu N     Dry lane worker CPU (default: 0)\n"
    << "  --wet-worker-cpu N     Wet lane worker CPU (default: 3)\n"
    << "  --pace                 Sleep between blocks at the requested quantum\n"
    << "  --no-rat               Omit the dry-lane Rat pre-stage\n"
    << "  --no-cab               Omit both cabinet IRs\n"
    << "  --cab-path PATH        Use this mono 48 kHz WAV for both cabinet lanes\n"
    << "  --relaxed-workers      Do not require FIFO scheduling/CPU affinity\n"
    << "  --only-pipeline        Measure only the worker-pipelined topology\n"
    << "  --only-direct          Measure only the direct-thread reference\n"
    << "  -h, --help             Show this help\n";
}

bool parsePositiveSize(std::string_view text, std::size_t& output)
{
  if (text.empty()) return false;
  std::size_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  if (value == 0) return false;
  output = value;
  return true;
}

bool parseCpu(std::string_view text, int& output)
{
  if (text == "0") {
    output = 0;
    return true;
  }
  std::size_t value = 0;
  if (!parsePositiveSize(text, value) || value > 1024) return false;
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
    } else if (argument == "--pipeline-slots") {
      const char* value = next();
      if (!value || !parsePositiveSize(value, options.pipelineSlots)
          || options.pipelineSlots < 2 || options.pipelineSlots > 16) return false;
    } else if (argument == "--audio-cpu") {
      const char* value = next();
      if (!value || !parseCpu(value, options.audioCpu)) return false;
    } else if (argument == "--dry-worker-cpu") {
      const char* value = next();
      if (!value || !parseCpu(value, options.dryWorkerCpu)) return false;
    } else if (argument == "--wet-worker-cpu") {
      const char* value = next();
      if (!value || !parseCpu(value, options.wetWorkerCpu)) return false;
    } else if (argument == "--pace") {
      options.pace = true;
    } else if (argument == "--no-rat") {
      options.includeRat = false;
    } else if (argument == "--no-cab") {
      options.includeCab = false;
    } else if (argument == "--cab-path") {
      const char* value = next();
      if (!value || *value == '\0') return false;
      options.cabPath = value;
      options.includeCab = true;
    } else if (argument == "--relaxed-workers") {
      options.relaxedWorkers = true;
    } else if (argument == "--only-pipeline") {
      options.onlyPipeline = true;
    } else if (argument == "--only-direct") {
      options.onlyDirect = true;
    } else if (!argument.empty() && argument.front() == '-') {
      return false;
    } else {
      options.models.emplace_back(argument);
    }
  }
  return !options.models.empty() && !(options.onlyPipeline && options.onlyDirect);
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

std::vector<float> makeCabImpulse(const Options& options)
{
  if (!options.cabPath.empty()) {
    auto wav = ardor::readMonoWav(options.cabPath);
    require(wav.sampleRate == static_cast<uint32_t>(kSampleRate),
            "cab WAV must be 48 kHz: " + options.cabPath.string());
    std::string error;
    require(ardor::prepareMonoIr(wav, kCabImpulseFrames, error),
            "failed to prepare cab WAV: " + error);
    return std::move(wav.samples);
  }

  std::vector<float> impulse(kCabImpulseFrames, 0.0f);
  for (std::size_t i = 0; i < impulse.size(); ++i) {
    const float decay = std::exp(-static_cast<float>(i) / 1600.0f);
    const float ripple = 0.72f + 0.28f * std::cos(static_cast<float>(i) * 0.013f);
    impulse[i] = decay * ripple * (i == 0 ? 0.9f : 0.035f);
  }
  return impulse;
}

std::vector<float> makeInput(std::size_t frames)
{
  std::vector<float> input(frames);
  std::uint32_t state = 0x6d2b79f5u;
  for (float& sample : input) {
    state = state * 1664525u + 1013904223u;
    sample = (static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f) * 0.25f;
  }
  return input;
}

std::unique_ptr<ardor::RuntimeChain> makeDryChain(const Options& options,
                                                   std::size_t blockSize,
                                                   bool includeRat)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  if (includeRat) {
    ardor::RatProcessor rat;
    std::string error;
    const nlohmann::json params{
      {"mode", "rat"}, {"distortion", 0.72f}, {"filter", 0.55f}, {"volume", 0.60f},
    };
    require(rat.configure(params, static_cast<float>(kSampleRate), error),
            "failed to configure dry Rat: " + error);
    chain->addDistortion("dry-rat", std::move(rat));
  }
  require(chain->addNam(options.models.front(), kSampleRate, static_cast<int>(blockSize), "dry-nam"),
          "failed to load dry NAM model: " + options.models.front().string());
  if (options.includeCab) chain->addCab(makeCabImpulse(options), 1.0f, 1.0f, "dry-cab");
  chain->prepareBlockSize(blockSize);
  return chain;
}

std::unique_ptr<ardor::RuntimeChain> makeWetChain(const Options& options,
                                                   std::size_t blockSize)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  const auto& model = options.models.size() > 1 ? options.models[1] : options.models.front();
  require(chain->addNam(model, kSampleRate, static_cast<int>(blockSize), "wet-nam"),
          "failed to load wet NAM model: " + model.string());
  if (options.includeCab) chain->addCab(makeCabImpulse(options), 1.0f, 1.0f, "wet-cab");

  std::string error;
  ardor::DaisyFxProcessor delay;
  const nlohmann::json delayParams{
    {"mode", "digital"}, {"time", 0.020f}, {"repeats", 0.35f}, {"mix", 0.38f},
    {"filter", 0.50f}, {"grit", 0.0f}, {"mod_spd", 0.0f}, {"mod_dep", 0.0f},
  };
  require(delay.configure("delay", delayParams, static_cast<float>(kSampleRate), error),
          "failed to configure wet delay: " + error);
  chain->addDaisy("wet-delay", std::move(delay));

  ardor::DaisyFxProcessor reverb;
  const nlohmann::json reverbParams{
    {"mode", "room"}, {"decay", 0.45f}, {"pre_delay", 0.0f}, {"mix", 0.35f},
    {"tone", 0.50f}, {"mod", 0.0f}, {"param1", 0.50f}, {"param2", 0.50f},
  };
  require(reverb.configure("reverb", reverbParams, static_cast<float>(kSampleRate), error),
          "failed to configure wet reverb: " + error);
  chain->addDaisy("wet-reverb", std::move(reverb));
  chain->prepareBlockSize(blockSize);
  return chain;
}

double percentile(std::vector<double> samples, double fraction)
{
  if (samples.empty()) return 0.0;
  std::sort(samples.begin(), samples.end());
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(samples.size() - 1)));
  return samples[index];
}

std::optional<std::size_t> calibrateFirstArrival(ardor::RuntimeChain& chain,
                                                 std::size_t blockSize)
{
  constexpr std::size_t kProbeBlocks = 256;
  constexpr float kArrivalThreshold = 1.0e-7f;
  std::vector<float> input(blockSize, 0.0f);
  std::vector<float> left(blockSize, 0.0f);
  std::vector<float> right(blockSize, 0.0f);
  std::optional<std::size_t> arrival;

  chain.reset();
  for (std::size_t block = 0; block < kProbeBlocks && !arrival; ++block) {
    std::fill(input.begin(), input.end(), 0.0f);
    if (block == 0) input.front() = 1.0f;
    chain.processBlock(input.data(), left.data(), right.data(), blockSize);
    for (std::size_t frame = 0; frame < blockSize; ++frame) {
      if (std::isfinite(left[frame]) && std::isfinite(right[frame])
          && std::max(std::fabs(left[frame]), std::fabs(right[frame]))
               > kArrivalThreshold) {
        arrival = block * blockSize + frame;
        break;
      }
    }
  }
  chain.reset();
  return arrival;
}

class WdwHarness {
public:
  WdwHarness(const Options& options, std::size_t blockSize, bool pipelined,
             bool includeRat)
    : dryChain_(makeDryChain(options, blockSize, includeRat)),
      wetChain_(makeWetChain(options, blockSize))
  {
    ardor::WdwPairExecutorOptions executorOptions;
    executorOptions.blockSize = blockSize;
    executorOptions.sampleRate = kSampleRate;
    executorOptions.mode = pipelined
      ? ardor::WdwPairExecutionMode::Pipelined
      : ardor::WdwPairExecutionMode::Direct;
    executorOptions.pipelineSlots = options.pipelineSlots;
    executorOptions.workerPriority = 69;
    executorOptions.requireWorkerSetup = !options.relaxedWorkers;
    executorOptions.requireRealtimeScheduling = !options.relaxedWorkers;
    executorOptions.requireAffinity = !options.relaxedWorkers;
    executorOptions.collectTiming = true;
    executorOptions.maxHoldBlocks = 1;

    const auto dryLatency = calibrateFirstArrival(*dryChain_, blockSize);
    const auto wetLatency = calibrateFirstArrival(*wetChain_, blockSize);
    require(dryLatency.has_value(), "dry lane produced no measurable impulse arrival");
    require(wetLatency.has_value(), "wet lane produced no measurable impulse arrival");
    dryLatencyFrames_ = dryLatency.value_or(0);
    wetLatencyFrames_ = wetLatency.value_or(0);

    ardor::WdwRoutingProgramOptions programOptions;
    programOptions.executor = executorOptions;
    programOptions.audioCpu = options.audioCpu;
    programOptions.mix = {1.0f, 0.0f, true, 1.0f, 1.0f, true};
    programOptions.dryLatencyFrames = dryLatencyFrames_;
    programOptions.wetLatencyFrames = wetLatencyFrames_;

    std::string error;
    program_ = std::make_unique<ardor::WdwRoutingProgram>();
    require(program_->prepare({"dry", std::move(dryChain_), options.dryWorkerCpu},
                              {"wet", std::move(wetChain_), options.wetWorkerCpu},
                              programOptions, error),
            "failed to configure WDW routing program: " + error);
  }

  WdwHarness(const WdwHarness&) = delete;
  WdwHarness& operator=(const WdwHarness&) = delete;

  bool workersReady() const noexcept
  {
    return program_->workersReady();
  }

  void process(const float* input, float* outputLeft, float* outputRight,
               std::size_t frames, bool timed)
  {
    ardor::WdwRoutingProcessResult result;
    require(program_->processBlock(input, outputLeft, outputRight, frames, result),
            "WDW routing program rejected block");
    maxPairAgeBlocks_ = std::max(maxPairAgeBlocks_, result.outputAgeBlocks);
    if (result.pairReady) ++pairReady_;
    if (!result.outputReady && pairReady_ == 0) ++initialSilence_;

    for (std::size_t i = 0; i < frames; ++i) {
      if (!std::isfinite(outputLeft[i]) || !std::isfinite(outputRight[i])) {
        ++nonFiniteOutputs_;
        outputLeft[i] = 0.0f;
        outputRight[i] = 0.0f;
      }
      if (timed) {
        wetStereoSpread_ += std::fabs(static_cast<double>(outputLeft[i]) - outputRight[i]);
      }
    }
  }

  std::uint64_t pairReady() const noexcept { return pairReady_; }
  std::uint64_t pairUnderflows() const noexcept { return program_->pairUnderflowCount(); }
  std::uint64_t pairSubmissionMisses() const noexcept
  {
    return program_->pairSubmissionMissCount();
  }
  std::size_t maxPairAgeBlocks() const noexcept { return maxPairAgeBlocks_; }
  std::uint64_t initialSilence() const noexcept { return initialSilence_; }
  std::uint64_t nonFiniteOutputs() const noexcept { return nonFiniteOutputs_; }
  double wetStereoSpread() const noexcept { return wetStereoSpread_; }

  LaneMetrics dryMetrics() const noexcept { return metrics(0); }
  LaneMetrics wetMetrics() const noexcept { return metrics(1); }
  std::uint64_t dryFaultBlocks() const noexcept { return program_->dryFaultBlockCount(); }
  std::uint64_t wetFaultBlocks() const noexcept { return program_->wetFaultBlockCount(); }
  std::size_t dryLatencyFrames() const noexcept { return dryLatencyFrames_; }
  std::size_t wetLatencyFrames() const noexcept { return wetLatencyFrames_; }

private:
  LaneMetrics metrics(std::size_t lane) const noexcept
  {
    const auto timing = program_->laneTiming(lane);
    return {
      timing.calls,
      timing.totalNanoseconds, timing.maximumNanoseconds, timing.requestedCpu,
      timing.actualCpu,
    };
  }

  std::unique_ptr<ardor::RuntimeChain> dryChain_;
  std::unique_ptr<ardor::RuntimeChain> wetChain_;
  std::unique_ptr<ardor::WdwRoutingProgram> program_;
  std::uint64_t pairReady_ = 0;
  std::size_t maxPairAgeBlocks_ = 0;
  std::uint64_t initialSilence_ = 0;
  std::uint64_t nonFiniteOutputs_ = 0;
  std::size_t dryLatencyFrames_ = 0;
  std::size_t wetLatencyFrames_ = 0;
  double wetStereoSpread_ = 0.0;
};

template <typename Process>
Stats measure(const Options& options, std::size_t frames, Process&& process)
{
  ardor::ScopedDenormalGuard denormals;
  const auto input = makeInput(frames);
  std::vector<float> outputLeft(frames, 0.0f);
  std::vector<float> outputRight(frames, 0.0f);
  std::vector<double> samples;
  samples.reserve(options.timedBlocks);
  const double budgetUs = static_cast<double>(frames) / kSampleRate * 1.0e6;
  const auto blockDuration = std::chrono::duration<double>(static_cast<double>(frames) / kSampleRate);
  auto nextBlock = std::chrono::steady_clock::now();
  auto paceBlock = [&]() {
    if (!options.pace) return;
    nextBlock += std::chrono::duration_cast<std::chrono::steady_clock::duration>(blockDuration);
    std::this_thread::sleep_until(nextBlock);
    if (std::chrono::steady_clock::now() > nextBlock + blockDuration) {
      nextBlock = std::chrono::steady_clock::now();
    }
  };

  double checksum = 0.0;
  for (std::size_t i = 0; i < options.warmupBlocks; ++i) {
    process(input.data(), outputLeft.data(), outputRight.data(), frames, false);
    paceBlock();
  }
  nextBlock = std::chrono::steady_clock::now();
  std::size_t deadlineMisses = 0;
  double sum = 0.0;
  for (std::size_t i = 0; i < options.timedBlocks; ++i) {
    const auto start = std::chrono::steady_clock::now();
    process(input.data(), outputLeft.data(), outputRight.data(), frames, true);
    const double elapsed = std::chrono::duration<double, std::micro>(
      std::chrono::steady_clock::now() - start).count();
    samples.push_back(elapsed);
    sum += elapsed;
    if (elapsed > budgetUs) ++deadlineMisses;
    checksum += static_cast<double>(outputLeft.front()) + outputRight.back();
    paceBlock();
  }

  Stats stats;
  stats.minimumUs = *std::min_element(samples.begin(), samples.end());
  stats.meanUs = sum / static_cast<double>(samples.size());
  stats.p99Us = percentile(samples, 0.99);
  stats.p999Us = percentile(samples, 0.999);
  stats.maximumUs = *std::max_element(samples.begin(), samples.end());
  stats.deadlineMisses = deadlineMisses;
  stats.checksum = checksum;
  return stats;
}

Row measureCase(const Options& options, std::size_t blockSize, bool pipelined,
                bool includeRat)
{
  WdwHarness harness(options, blockSize, pipelined, includeRat);
  Row row;
  row.scenario = pipelined ? "wdw-pipeline" : "wdw-direct";
  row.blockSize = blockSize;
  row.pipelined = pipelined;
  row.rat = includeRat;
  row.cab = options.includeCab;
  row.workersReady = harness.workersReady();
  row.stats = measure(options, blockSize,
                      [&](const float* input, float* left, float* right,
                          std::size_t frames, bool timed) {
                        harness.process(input, left, right, frames, timed);
                      });
  row.pairReady = harness.pairReady();
  row.pairUnderflows = harness.pairUnderflows();
  row.pairSubmissionMisses = harness.pairSubmissionMisses();
  row.maxPairAgeBlocks = harness.maxPairAgeBlocks();
  row.initialSilence = harness.initialSilence();
  row.nonFiniteOutputs = harness.nonFiniteOutputs();
  row.dryFaultBlocks = harness.dryFaultBlocks();
  row.wetFaultBlocks = harness.wetFaultBlocks();
  row.dryLatencyFrames = harness.dryLatencyFrames();
  row.wetLatencyFrames = harness.wetLatencyFrames();
  row.wetStereoSpread = harness.wetStereoSpread();
  row.dry = harness.dryMetrics();
  row.wet = harness.wetMetrics();
  return row;
}

void printHeader()
{
  std::cout << "scenario,block_size,rat,cab,pipelined,workers_ready,"
               "pair_ready,pair_underflows,pair_submission_misses,max_pair_age_blocks,initial_silence,nonfinite_outputs,"
               "dry_fault_blocks,wet_fault_blocks,dry_latency_frames,wet_latency_frames,wet_stereo_spread,"
               "deadline_misses,min_us,mean_us,p99_us,p999_us,max_us,checksum,"
               "dry_worker_calls,dry_worker_mean_us,dry_worker_max_us,dry_requested_cpu,dry_actual_cpu,"
               "wet_worker_calls,wet_worker_mean_us,wet_worker_max_us,wet_requested_cpu,wet_actual_cpu\n";
}

void printRow(const Row& row)
{
  const auto meanWorkerUs = [](const LaneMetrics& lane) {
    return lane.timingCalls == 0 ? 0.0
      : static_cast<double>(lane.timingTotalNs) / static_cast<double>(lane.timingCalls) / 1000.0;
  };
  std::cout << row.scenario << ',' << row.blockSize << ',' << (row.rat ? 1 : 0) << ','
            << (row.cab ? 1 : 0) << ',' << (row.pipelined ? 1 : 0) << ','
            << (row.workersReady ? 1 : 0) << ',' << row.pairReady << ','
            << row.pairUnderflows << ',' << row.pairSubmissionMisses << ','
            << row.maxPairAgeBlocks << ','
            << row.initialSilence << ',' << row.nonFiniteOutputs << ','
            << row.dryFaultBlocks << ',' << row.wetFaultBlocks << ','
            << row.dryLatencyFrames << ',' << row.wetLatencyFrames << ','
            << std::setprecision(12) << row.wetStereoSpread << ',' << row.stats.deadlineMisses << ','
            << row.stats.minimumUs << ',' << row.stats.meanUs << ',' << row.stats.p99Us << ','
            << row.stats.p999Us << ',' << row.stats.maximumUs << ',' << row.stats.checksum << ','
            << row.dry.timingCalls << ',' << meanWorkerUs(row.dry) << ','
            << static_cast<double>(row.dry.timingMaximumNs) / 1000.0 << ','
            << row.dry.requestedCpu << ',' << row.dry.actualCpu << ','
            << row.wet.timingCalls << ',' << meanWorkerUs(row.wet) << ','
            << static_cast<double>(row.wet.timingMaximumNs) / 1000.0 << ','
            << row.wet.requestedCpu << ',' << row.wet.actualCpu << '\n';
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
    std::cerr << "wdw-feasibility: audio_cpu=" << options.audioCpu
              << " pinned=" << (pinned ? 1 : 0)
              << " realtime=" << (realtime ? 1 : 0)
              << " models=" << options.models.size()
              << " rat=" << (options.includeRat ? 1 : 0)
              << " cab=" << (options.includeCab ? 1 : 0)
              << " workers=" << options.dryWorkerCpu << ',' << options.wetWorkerCpu
              << " pipeline_slots=" << options.pipelineSlots
              << " relaxed_workers=" << (options.relaxedWorkers ? 1 : 0) << '\n';

    printHeader();
    for (const std::size_t blockSize : options.blockSizes) {
      if (!options.onlyDirect) printRow(measureCase(options, blockSize, true, options.includeRat));
      if (!options.onlyPipeline) printRow(measureCase(options, blockSize, false, options.includeRat));
    }
  } catch (const std::exception& error) {
    std::cerr << "wdw-feasibility: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
