// Dedicated Neural Amp Modeler benchmark for target hardware.
//
// Unlike dsp_bench.cpp, this executable accepts whole model collections and
// emits machine-readable CSV.  It is intentionally not a ctest: scheduling
// noise and thermal state are properties of the device being measured.

#include "dsp/DenormalGuard.h"
#include "dsp/NamProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace {

constexpr double kSampleRate = 48000.0;

struct Options {
  std::vector<std::filesystem::path> inputs;
  std::vector<std::size_t> blockSizes{8, 16, 32, 64, 128};
  std::size_t warmupBlocks = 200;
  std::size_t timedBlocks = 2000;
  std::string tiers = "all";
  int cpu = -1;
};

struct Tier {
  std::string name;
  float value = 1.0f;
};

struct Result {
  double minimumUs = 0.0;
  double meanUs = 0.0;
  double standardDeviationUs = 0.0;
  double p50Us = 0.0;
  double p90Us = 0.0;
  double p99Us = 0.0;
  double p999Us = 0.0;
  double p9999Us = 0.0;
  double maximumUs = 0.0;
  std::size_t deadlineMisses = 0;
  double checksum = 0.0;
};

void usage(const char* program)
{
  std::cerr
    << "Usage: " << program << " [options] MODEL_OR_DIRECTORY...\n"
    << "Options:\n"
    << "  --block-sizes N,N,...  Audio quanta to test (default: 8,16,32,64,128)\n"
    << "  --warmup N              Untimed blocks per case (default: 200)\n"
    << "  --iterations N          Timed blocks per case (default: 2000)\n"
    << "  --tiers all|full|nano   Slimmable tiers to test (default: all)\n"
    << "  --cpu N                 Pin the benchmark to one CPU\n"
    << "  -h, --help              Show this help\n";
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

bool parseBlockSizes(std::string_view text, std::vector<std::size_t>& output)
{
  output.clear();
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t comma = text.find(',', begin);
    const auto token = text.substr(begin, comma == std::string_view::npos ? text.size() - begin
                                                                          : comma - begin);
    std::size_t value = 0;
    if (!parsePositiveSize(token, value) || value > 4096) return false;
    output.push_back(value);
    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }
  std::sort(output.begin(), output.end());
  output.erase(std::unique(output.begin(), output.end()), output.end());
  return !output.empty();
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
      if (!value || !parseBlockSizes(value, options.blockSizes)) return false;
    } else if (argument == "--warmup") {
      const char* value = next();
      if (!value || !parsePositiveSize(value, options.warmupBlocks)) return false;
    } else if (argument == "--iterations") {
      const char* value = next();
      if (!value || !parsePositiveSize(value, options.timedBlocks)) return false;
    } else if (argument == "--tiers") {
      const char* value = next();
      if (!value) return false;
      options.tiers = value;
      if (options.tiers != "all" && options.tiers != "full" && options.tiers != "nano") {
        return false;
      }
    } else if (argument == "--cpu") {
      const char* value = next();
      std::size_t cpu = 0;
      if (!value || !parsePositiveSize(value, cpu) || cpu > static_cast<std::size_t>(INT32_MAX)) {
        // CPU 0 is valid, unlike the other positive-size options.
        if (!value || std::string_view{value} != "0") return false;
        cpu = 0;
      }
      options.cpu = static_cast<int>(cpu);
    } else if (!argument.empty() && argument.front() == '-') {
      return false;
    } else {
      options.inputs.emplace_back(argument);
    }
  }
  return !options.inputs.empty();
}

bool isModelPath(const std::filesystem::path& path)
{
  const auto filename = path.filename().string();
  return path.extension() == ".nam" && filename.rfind("._", 0) != 0;
}

std::vector<std::filesystem::path> collectModels(const std::vector<std::filesystem::path>& inputs)
{
  std::vector<std::filesystem::path> models;
  for (const auto& input : inputs) {
    std::error_code error;
    if (std::filesystem::is_regular_file(input, error)) {
      if (isModelPath(input)) models.push_back(input);
      continue;
    }
    if (!std::filesystem::is_directory(input, error)) {
      std::cerr << "nam-bench: input does not exist: " << input << "\n";
      continue;
    }
    std::filesystem::recursive_directory_iterator iterator{
      input, std::filesystem::directory_options::skip_permission_denied, error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
      if (iterator->is_regular_file(error) && !error && isModelPath(iterator->path())) {
        models.push_back(iterator->path());
      }
      iterator.increment(error);
    }
    if (error) {
      std::cerr << "nam-bench: directory scan warning for " << input << ": "
                << error.message() << "\n";
    }
  }
  std::sort(models.begin(), models.end());
  models.erase(std::unique(models.begin(), models.end()), models.end());
  return models;
}

std::string csv(std::string_view text)
{
  std::string output{"\""};
  for (const char c : text) {
    if (c == '"') output += '"';
    output += c;
  }
  output += '"';
  return output;
}

double percentile(const std::vector<double>& sorted, double fraction)
{
  if (sorted.empty()) return 0.0;
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(sorted.size() - 1)));
  return sorted[index];
}

uint32_t nextNoise(uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

Result benchmark(ardor::NamProcessor& processor, std::size_t frames,
                 std::size_t warmupBlocks, std::size_t timedBlocks)
{
  std::vector<float> input(frames);
  std::vector<float> output(frames);
  uint32_t noise = 0x6d2b79f5u;
  for (float& sample : input) {
    sample = (static_cast<float>(nextNoise(noise) >> 8) / static_cast<float>(1u << 24) - 0.5f)
             * 0.25f;
  }

  for (std::size_t block = 0; block < warmupBlocks; ++block) {
    processor.processBlock(input.data(), output.data(), frames);
  }

  const double budgetUs = static_cast<double>(frames) / kSampleRate * 1.0e6;
  std::vector<double> samples;
  samples.reserve(timedBlocks);
  double sum = 0.0;
  double sumSquares = 0.0;
  double checksum = 0.0;
  std::size_t misses = 0;
  for (std::size_t block = 0; block < timedBlocks; ++block) {
    const auto start = std::chrono::steady_clock::now();
    processor.processBlock(input.data(), output.data(), frames);
    const auto end = std::chrono::steady_clock::now();
    const double elapsedUs = std::chrono::duration<double, std::micro>(end - start).count();
    samples.push_back(elapsedUs);
    sum += elapsedUs;
    sumSquares += elapsedUs * elapsedUs;
    if (elapsedUs > budgetUs) ++misses;
    checksum += output[block % frames];
  }

  std::sort(samples.begin(), samples.end());
  const double mean = sum / static_cast<double>(timedBlocks);
  const double variance = std::max(0.0, sumSquares / static_cast<double>(timedBlocks) - mean * mean);
  return {
    samples.front(), mean, std::sqrt(variance),
    percentile(samples, 0.50), percentile(samples, 0.90), percentile(samples, 0.99),
    percentile(samples, 0.999), percentile(samples, 0.9999), samples.back(),
    misses, checksum,
  };
}

std::vector<Tier> requestedTiers(const std::string& request, const std::vector<double>& breakpoints)
{
  if (request == "full") return {{"full", 1.0f}};
  if (request == "nano") return {{"nano", 0.0f}};
  if (breakpoints.empty()) return {{"fixed", 1.0f}};

  std::vector<Tier> tiers;
  tiers.reserve(breakpoints.size() + 1);
  for (std::size_t index = 0; index <= breakpoints.size(); ++index) {
    float value = index == 0 ? 0.0f : static_cast<float>(breakpoints[index - 1]);
    std::string name = "tier-" + std::to_string(index);
    if (breakpoints.size() == 1) name = index == 0 ? "nano" : "full";
    tiers.push_back({std::move(name), value});
  }
  return tiers;
}

void printMetadata(const Options& options, std::size_t modelCount)
{
#if defined(__aarch64__)
  constexpr const char* architecture = "aarch64";
#elif defined(__x86_64__)
  constexpr const char* architecture = "x86_64";
#else
  constexpr const char* architecture = "other";
#endif
  std::cout << "# benchmark=ardor-nam-bench-v1\n"
            << "# build_type=" << ARDOR_BUILD_TYPE << "\n"
            << "# compiler=" << ARDOR_COMPILER << "\n"
            << "# architecture=" << architecture << "\n"
            << "# compile_flags=" << ARDOR_CXX_FLAGS << "\n"
            << "# sample_rate=" << static_cast<unsigned>(kSampleRate) << "\n"
            << "# models=" << modelCount << "\n"
            << "# warmup_blocks=" << options.warmupBlocks << "\n"
            << "# timed_blocks=" << options.timedBlocks << "\n";
#if defined(__linux__)
  if (options.cpu >= 0) {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(options.cpu, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus) != 0) {
      std::cerr << "nam-bench: failed to pin CPU " << options.cpu << ": "
                << std::strerror(errno) << "\n";
      std::exit(1);
    }
  }
  std::cout << "# cpu=" << sched_getcpu() << "\n";
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
    std::cout << "# mlockall=ok\n";
  } else {
    std::cout << "# mlockall=failed:" << errno << ":" << std::strerror(errno) << "\n";
  }
#endif
  std::cout
    << "model,tier,tier_value,block_frames,budget_us,load_us,iterations,min_us,p50_us,p90_us,"
       "p99_us,p999_us,p9999_us,max_us,mean_us,stddev_us,deadline_misses,mean_budget_pct,"
       "p999_budget_pct,realtime_factor,checksum\n";
}

} // namespace

int main(int argc, char** argv)
{
  Options options;
  if (!parseArgs(argc, argv, options)) {
    usage(argv[0]);
    return 2;
  }

  const auto models = collectModels(options.inputs);
  if (models.empty()) {
    std::cerr << "nam-bench: no .nam models found\n";
    return 1;
  }

  const ardor::ScopedDenormalGuard denormalGuard;
  printMetadata(options, models.size());
  std::cout << std::fixed << std::setprecision(4);

  std::size_t failures = 0;
  for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
    const auto& modelPath = models[modelIndex];
    std::cerr << "nam-bench: [" << (modelIndex + 1) << "/" << models.size() << "] "
              << modelPath << "\n";

    // Load once to discover the tier layout. Each measured case is loaded
    // afresh below so its prewarm size matches the production audio quantum.
    ardor::NamProcessor probe;
    if (!probe.load(modelPath, kSampleRate, static_cast<int>(options.blockSizes.back()))) {
      std::cerr << "nam-bench: failed to load " << modelPath << "\n";
      ++failures;
      continue;
    }
    const auto tiers = requestedTiers(options.tiers, probe.slimmableSizeBreakpoints());
    probe.clear();

    for (const auto& tier : tiers) {
      for (const std::size_t blockSize : options.blockSizes) {
        ardor::NamProcessor processor;
        const auto loadStart = std::chrono::steady_clock::now();
        const bool loaded = processor.load(modelPath, kSampleRate, static_cast<int>(blockSize), tier.value);
        const double loadUs = std::chrono::duration<double, std::micro>(
          std::chrono::steady_clock::now() - loadStart).count();
        if (!loaded) {
          std::cerr << "nam-bench: failed to load " << modelPath << " at block " << blockSize << "\n";
          ++failures;
          continue;
        }

        const Result result = benchmark(
          processor, blockSize, options.warmupBlocks, options.timedBlocks);
        const double budgetUs = static_cast<double>(blockSize) / kSampleRate * 1.0e6;
        std::cout
          << csv(modelPath.string()) << ',' << csv(tier.name) << ',' << tier.value << ','
          << blockSize << ',' << budgetUs << ',' << loadUs << ',' << options.timedBlocks << ','
          << result.minimumUs << ',' << result.p50Us << ',' << result.p90Us << ','
          << result.p99Us << ',' << result.p999Us << ',' << result.p9999Us << ','
          << result.maximumUs << ',' << result.meanUs << ',' << result.standardDeviationUs << ','
          << result.deadlineMisses << ',' << result.meanUs * 100.0 / budgetUs << ','
          << result.p999Us * 100.0 / budgetUs << ',' << budgetUs / result.meanUs << ','
          << result.checksum << '\n';
        std::cout.flush();
      }
    }
  }

  if (failures != 0) {
    std::cerr << "nam-bench: " << failures << " case(s) failed\n";
    return 1;
  }
  return 0;
}
