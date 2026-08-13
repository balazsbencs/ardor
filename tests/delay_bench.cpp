// Dedicated hosted-delay benchmark for target hardware.
//
// The general DSP benchmark is useful for broad comparisons, but its fixed
// 64-frame quantum and human-readable output make before/after delay work hard
// to compare. This executable covers every delay mode at production quanta and
// emits stable CSV. It is intentionally not a ctest: timing is not pass/fail on
// shared hosts.

#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DenormalGuard.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace {

constexpr double kSampleRate = 48000.0;

struct Options {
  std::vector<std::size_t> blockSizes{32, 64};
  std::size_t warmupBlocks = 500;
  std::size_t timedBlocks = 10000;
  int cpu = -1;
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
    << "Usage: " << program << " [options]\n"
    << "Options:\n"
    << "  --block-sizes N,N,...  Audio quanta to test (default: 32,64)\n"
    << "  --warmup N              Untimed blocks per case (default: 500)\n"
    << "  --iterations N          Timed blocks per case (default: 10000)\n"
    << "  --cpu N                 Pin the benchmark to one CPU\n"
    << "  -h, --help              Show this help\n";
}

bool parseNonNegativeSize(std::string_view text, std::size_t& output)
{
  if (text.empty()) return false;
  std::size_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  output = value;
  return true;
}

bool parsePositiveSize(std::string_view text, std::size_t& output)
{
  return parseNonNegativeSize(text, output) && output != 0;
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
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
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
    } else if (argument == "--cpu") {
      const char* value = next();
      std::size_t parsed = 0;
      if (!value || !parseNonNegativeSize(value, parsed)
          || parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
      options.cpu = static_cast<int>(parsed);
    } else {
      return false;
    }
  }
  return true;
}

uint32_t nextNoise(uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

double percentile(const std::vector<double>& sorted, double fraction)
{
  if (sorted.empty()) return 0.0;
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(sorted.size() - 1)));
  return sorted[index];
}

nlohmann::json paramsFor(const ardor::DaisyFxDescriptor& descriptor, std::string_view scenario)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  if (scenario == "stress") {
    for (const auto& parameter : descriptor.params) params[parameter.key] = 1.0f;
  } else if (scenario == "automation") {
    params["mix"] = 1.0f;
    params["repeats"] = 0.75f;
    params["filter"] = 0.5f;
    params["grit"] = 0.5f;
    params["mod_spd"] = 0.65f;
    params["mod_dep"] = 0.7f;
    params["time"] = 0.2f;
  }
  return params;
}

Result benchmark(const ardor::DaisyFxDescriptor& descriptor, std::string_view scenario,
                 std::size_t frames, std::size_t warmupBlocks, std::size_t timedBlocks)
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("delay", paramsFor(descriptor, scenario),
                           static_cast<float>(kSampleRate), error)) {
    throw std::runtime_error(descriptor.mode + ": " + error);
  }

  std::vector<ardor::StereoSample> input(frames);
  std::vector<ardor::StereoSample> output(frames);
  uint32_t leftNoise = 0x6d2b79f5u;
  uint32_t rightNoise = 0x9e3779b9u;
  for (auto& sample : input) {
    sample.left = (static_cast<float>(nextNoise(leftNoise) >> 8) /
                     static_cast<float>(1u << 24) - 0.5f) * 0.5f;
    sample.right = (static_cast<float>(nextNoise(rightNoise) >> 8) /
                      static_cast<float>(1u << 24) - 0.5f) * 0.5f;
  }

  auto automate = [&](std::size_t block) {
    if (scenario == "automation") {
      const std::size_t period = 512;
      const float phase = static_cast<float>(block % period) / static_cast<float>(period - 1);
      const float triangle = phase <= 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
      processor.setParameterTarget("time", 0.1f + triangle * 0.8f);
    }
  };
  auto processBlock = [&]() {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      output[frame] = processor.process(input[frame]);
    }
  };

  for (std::size_t block = 0; block < warmupBlocks; ++block) {
    automate(block);
    processBlock();
  }

  const double budgetUs = static_cast<double>(frames) / kSampleRate * 1.0e6;
  std::vector<double> samples;
  samples.reserve(timedBlocks);
  double sum = 0.0;
  double sumSquares = 0.0;
  double checksum = 0.0;
  std::size_t misses = 0;
  for (std::size_t block = 0; block < timedBlocks; ++block) {
    // Target publication happens on the control thread in production. Keep it
    // outside the timed audio callback while retaining the resulting control-
    // rate tap transitions inside the measurement.
    automate(block + warmupBlocks);
    const auto start = std::chrono::steady_clock::now();
    processBlock();
    const auto end = std::chrono::steady_clock::now();
    const double elapsedUs = std::chrono::duration<double, std::micro>(end - start).count();
    samples.push_back(elapsedUs);
    sum += elapsedUs;
    sumSquares += elapsedUs * elapsedUs;
    if (elapsedUs > budgetUs) ++misses;
    const auto& sample = output[block % frames];
    checksum += static_cast<double>(sample.left) + 0.5 * static_cast<double>(sample.right);
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

void printMetadata(const Options& options)
{
#if defined(__aarch64__)
  constexpr const char* architecture = "aarch64";
#elif defined(__x86_64__)
  constexpr const char* architecture = "x86_64";
#else
  constexpr const char* architecture = "other";
#endif
  std::cout << "# benchmark=ardor-delay-bench-v1\n"
            << "# build_type=" << ARDOR_BUILD_TYPE << "\n"
            << "# compiler=" << ARDOR_COMPILER << "\n"
            << "# architecture=" << architecture << "\n"
            << "# compile_flags=" << ARDOR_CXX_FLAGS << "\n"
            << "# sample_rate=" << static_cast<unsigned>(kSampleRate) << "\n"
            << "# warmup_blocks=" << options.warmupBlocks << "\n"
            << "# timed_blocks=" << options.timedBlocks << "\n";
#if defined(__linux__)
  if (options.cpu >= 0) {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(options.cpu, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus) != 0) {
      std::cerr << "delay-bench: failed to pin CPU " << options.cpu << ": "
                << std::strerror(errno) << "\n";
      std::exit(1);
    }
  }
  std::cout << "# cpu=" << sched_getcpu() << "\n";
  // Each case allocates its processor after metadata is printed. Locking all
  // future allocations can make the larger full-range Pattern buffer fail
  // under a typical 8 MiB memlock limit. Warm-up touches every delay page, so
  // locking the current executable image is sufficient and allocation-safe.
  if (mlockall(MCL_CURRENT) == 0) {
    std::cout << "# mlockall=ok\n";
  } else {
    std::cout << "# mlockall=failed:" << errno << ':' << std::strerror(errno) << "\n";
  }
#endif
  std::cout
    << "mode,scenario,block_frames,budget_us,iterations,min_us,p50_us,p90_us,p99_us,"
       "p999_us,p9999_us,max_us,mean_us,stddev_us,deadline_misses,mean_budget_pct,"
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

  const ardor::ScopedDenormalGuard denormalGuard;
  printMetadata(options);
  std::cout << std::fixed << std::setprecision(4);

  try {
    for (const auto& descriptor : ardor::daisyFxCatalog()) {
      if (descriptor.blockType != "delay") continue;
      for (const std::string_view scenario : {"default", "stress", "automation"}) {
        for (const std::size_t blockSize : options.blockSizes) {
          const Result result = benchmark(
            descriptor, scenario, blockSize, options.warmupBlocks, options.timedBlocks);
          const double budgetUs = static_cast<double>(blockSize) / kSampleRate * 1.0e6;
          std::cout << descriptor.mode << ',' << scenario << ',' << blockSize << ',' << budgetUs
                    << ',' << options.timedBlocks << ',' << result.minimumUs << ',' << result.p50Us
                    << ',' << result.p90Us << ',' << result.p99Us << ',' << result.p999Us << ','
                    << result.p9999Us << ',' << result.maximumUs << ',' << result.meanUs << ','
                    << result.standardDeviationUs << ',' << result.deadlineMisses << ','
                    << result.meanUs * 100.0 / budgetUs << ','
                    << result.p999Us * 100.0 / budgetUs << ',' << budgetUs / result.meanUs << ','
                    << result.checksum << '\n';
          std::cout.flush();
        }
      }
    }
  } catch (const std::exception& exception) {
    std::cerr << "delay-bench: " << exception.what() << '\n';
    return 1;
  }
  return 0;
}
