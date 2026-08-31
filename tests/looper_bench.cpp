// Standalone looper timing probe for target hardware. This is intentionally
// not a CTest: timing distributions on a shared development host are evidence,
// not a portable pass/fail assertion.

#include "looper/RealtimeLooper.h"

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
  std::vector<std::size_t> blockSizes {32, 64, 128};
  std::size_t warmupBlocks = 500;
  std::size_t timedBlocks = 10000;
  int cpu = -1;
};

struct Result {
  double minimumUs = 0.0;
  double meanUs = 0.0;
  double p99Us = 0.0;
  double p999Us = 0.0;
  double maximumUs = 0.0;
  std::size_t deadlineMisses = 0;
  double checksum = 0.0;
};

void usage(const char* program)
{
  std::cerr << "Usage: " << program << " [--block-sizes 32,64,128]"
               " [--warmup N] [--iterations N] [--cpu N]\n";
}

bool parseSize(std::string_view text, std::size_t& value, bool allowZero = false)
{
  if (text.empty()) return false;
  value = 0;
  for (char character : text) {
    if (character < '0' || character > '9') return false;
    const auto digit = static_cast<std::size_t>(character - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return allowZero || value > 0;
}

bool parseBlockSizes(std::string_view text, std::vector<std::size_t>& sizes)
{
  sizes.clear();
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto token = text.substr(0, comma);
    std::size_t size = 0;
    if (!parseSize(token, size) || size > 4096) return false;
    sizes.push_back(size);
    if (comma == std::string_view::npos) break;
    text.remove_prefix(comma + 1);
  }
  std::sort(sizes.begin(), sizes.end());
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  return !sizes.empty();
}

bool parseArgs(int argc, char** argv, Options& options)
{
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument {argv[index]};
    const auto next = [&]() -> const char* {
      return index + 1 < argc ? argv[++index] : nullptr;
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
      if (!value || !parseSize(value, options.warmupBlocks)) return false;
    } else if (argument == "--iterations") {
      const char* value = next();
      if (!value || !parseSize(value, options.timedBlocks)) return false;
    } else if (argument == "--cpu") {
      const char* value = next();
      std::size_t cpu = 0;
      if (!value || !parseSize(value, cpu, true)
          || cpu > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
      options.cpu = static_cast<int>(cpu);
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
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(sorted.size() - 1)));
  return sorted[index];
}

template <typename Process>
Result measure(Process&& process, std::size_t frames,
               std::size_t warmupBlocks, std::size_t timedBlocks)
{
  std::vector<float> left(frames);
  std::vector<float> right(frames);
  uint32_t leftNoise = 0x6d2b79f5u;
  uint32_t rightNoise = 0x9e3779b9u;
  const auto fillInput = [&] {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      left[frame] = (static_cast<float>(nextNoise(leftNoise) >> 8)
                     / static_cast<float>(1U << 24) - 0.5f) * 0.25f;
      right[frame] = (static_cast<float>(nextNoise(rightNoise) >> 8)
                      / static_cast<float>(1U << 24) - 0.5f) * 0.25f;
    }
  };
  for (std::size_t block = 0; block < warmupBlocks; ++block) {
    fillInput();
    process(left.data(), right.data(), frames);
  }

  const double budgetUs = static_cast<double>(frames) / kSampleRate * 1.0e6;
  std::vector<double> samples;
  samples.reserve(timedBlocks);
  double totalUs = 0.0;
  double checksum = 0.0;
  std::size_t misses = 0;
  for (std::size_t block = 0; block < timedBlocks; ++block) {
    fillInput();
    const auto start = std::chrono::steady_clock::now();
    process(left.data(), right.data(), frames);
    const double elapsedUs = std::chrono::duration<double, std::micro>(
      std::chrono::steady_clock::now() - start).count();
    samples.push_back(elapsedUs);
    totalUs += elapsedUs;
    if (elapsedUs > budgetUs) ++misses;
    checksum += left[block % frames] + 0.5 * right[block % frames];
  }
  std::sort(samples.begin(), samples.end());
  return {samples.front(), totalUs / static_cast<double>(timedBlocks),
          percentile(samples, 0.99), percentile(samples, 0.999), samples.back(),
          misses, checksum};
}

struct LooperFixture {
  explicit LooperFixture(std::size_t blockFrames, std::size_t maximumFrames)
    : blockFrames(blockFrames), left(blockFrames), right(blockFrames)
  {
    std::string error;
    if (!looper.prepare(static_cast<float>(kSampleRate), blockFrames,
                        maximumFrames * ardor::RealtimeLooper::kBytesPerMasterFrame, error)) {
      throw std::runtime_error(error);
    }
  }

  void command(ardor::LooperCommandType type, std::size_t track = 0)
  {
    if (!looper.tryEnqueue({sequence++, type, static_cast<uint8_t>(track), 0.0f})) {
      throw std::runtime_error("looper command queue full during benchmark setup");
    }
  }

  void process(float value = 0.05f)
  {
    std::fill(left.begin(), left.end(), value);
    std::fill(right.begin(), right.end(), -value);
    looper.processBlock(left.data(), right.data(), blockFrames);
  }

  ardor::LooperTelemetry telemetry()
  {
    ardor::LooperTelemetry latest;
    ardor::LooperTelemetry next;
    while (looper.tryReadTelemetry(next)) latest = next;
    return latest;
  }

  void open()
  {
    command(ardor::LooperCommandType::OpenEmpty);
    process();
  }

  void recordMaster(std::size_t loopFrames)
  {
    open();
    command(ardor::LooperCommandType::RecordOrOverdub, 0);
    for (std::size_t frame = 0; frame < loopFrames; frame += blockFrames) process();
    command(ardor::LooperCommandType::RecordOrOverdub, 0);
    process();
  }

  void addFollower(std::size_t track, std::size_t loopFrames)
  {
    command(ardor::LooperCommandType::RecordOrOverdub, track);
    const auto maximumBlocks = loopFrames / blockFrames * 3 + 4;
    for (std::size_t block = 0; block < maximumBlocks; ++block) {
      process(0.02f * static_cast<float>(track + 1));
      if (telemetry().tracks[track].state == ardor::LooperTrackState::Playing) return;
    }
    throw std::runtime_error("follower did not finish during benchmark setup");
  }

  std::size_t blockFrames;
  ardor::RealtimeLooper looper;
  std::vector<float> left;
  std::vector<float> right;
  uint64_t sequence = 1;
};

class DryMonoReference {
public:
  DryMonoReference(std::size_t frames, std::size_t trackCount)
    : tracks_(trackCount, std::vector<float>(frames)), frames_(frames)
  {
    for (std::size_t track = 0; track < tracks_.size(); ++track) {
      for (std::size_t frame = 0; frame < frames_; ++frame) {
        tracks_[track][frame] = 0.01f * static_cast<float>(track + 1)
          * std::sin(static_cast<float>(frame) * 0.001f);
      }
    }
  }

  void process(float* left, float* right, std::size_t blockFrames) noexcept
  {
    for (std::size_t frame = 0; frame < blockFrames; ++frame) {
      float mixed = 0.5f * (left[frame] + right[frame]);
      for (const auto& track : tracks_) mixed += track[playhead_];
      left[frame] = mixed;
      right[frame] = mixed;
      if (++playhead_ == frames_) playhead_ = 0;
    }
  }

private:
  std::vector<std::vector<float>> tracks_;
  std::size_t frames_ = 0;
  std::size_t playhead_ = 0;
};

void printResult(std::string_view mode, std::size_t blockFrames, std::size_t iterations,
                 std::size_t estimatedBytesPerFrame, const Result& result)
{
  const double budgetUs = static_cast<double>(blockFrames) / kSampleRate * 1.0e6;
  const double bandwidthGiBs = result.meanUs > 0.0
    ? static_cast<double>(estimatedBytesPerFrame * blockFrames) / (result.meanUs * 1.0e-6)
        / static_cast<double>(1ULL << 30)
    : 0.0;
  std::cout << mode << ',' << blockFrames << ',' << budgetUs << ',' << iterations << ','
            << result.minimumUs << ',' << result.p99Us << ',' << result.p999Us << ','
            << result.maximumUs << ',' << result.meanUs << ',' << result.deadlineMisses << ','
            << result.meanUs * 100.0 / budgetUs << ',' << result.p999Us * 100.0 / budgetUs << ','
            << estimatedBytesPerFrame << ',' << bandwidthGiBs << ',' << result.checksum << '\n';
}

void configureHost(const Options& options)
{
#if defined(__aarch64__)
  constexpr const char* architecture = "aarch64";
#elif defined(__x86_64__)
  constexpr const char* architecture = "x86_64";
#else
  constexpr const char* architecture = "other";
#endif
  std::cout << "# benchmark=ardor-looper-bench-v1\n"
            << "# build_type=" << ARDOR_BUILD_TYPE << "\n"
            << "# compiler=" << ARDOR_COMPILER << "\n"
            << "# architecture=" << architecture << "\n"
            << "# compile_flags=" << ARDOR_CXX_FLAGS << "\n"
            << "# sample_rate=48000\n"
            << "# warmup_blocks=" << options.warmupBlocks << "\n"
            << "# timed_blocks=" << options.timedBlocks << "\n";
#if defined(__linux__)
  if (options.cpu >= 0) {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(options.cpu, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus) != 0) {
      throw std::runtime_error("failed to pin benchmark CPU: " + std::string(std::strerror(errno)));
    }
  }
  std::cout << "# cpu=" << sched_getcpu() << "\n";
  if (mlockall(MCL_CURRENT) == 0) std::cout << "# mlockall=ok\n";
  else std::cout << "# mlockall=failed:" << errno << ':' << std::strerror(errno) << "\n";
#endif
  std::cout << "mode,block_frames,budget_us,iterations,min_us,p99_us,p999_us,max_us,"
               "mean_us,deadline_misses,mean_budget_pct,p999_budget_pct,"
               "estimated_bytes_per_frame,estimated_gib_per_s,checksum\n";
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
    configureHost(options);
    std::cout << std::fixed << std::setprecision(4);
    for (const auto blockFrames : options.blockSizes) {
      const auto measuredFrames = (options.warmupBlocks + options.timedBlocks + 8) * blockFrames;
      const auto loopFrames = ((48000 + blockFrames - 1) / blockFrames) * blockFrames;

      {
        LooperFixture inactive(blockFrames, loopFrames + blockFrames);
        printResult("inactive", blockFrames, options.timedBlocks, 0,
          measure([&](float* left, float* right, std::size_t frames) {
            inactive.looper.processBlock(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      {
        LooperFixture recording(blockFrames, measuredFrames + blockFrames);
        recording.open();
        recording.command(ardor::LooperCommandType::RecordOrOverdub, 0);
        printResult("processed_stereo_record", blockFrames, options.timedBlocks, 8,
          measure([&](float* left, float* right, std::size_t frames) {
            recording.looper.processBlock(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      {
        LooperFixture oneTrack(blockFrames, loopFrames + blockFrames);
        oneTrack.recordMaster(loopFrames);
        printResult("processed_stereo_1_play", blockFrames, options.timedBlocks, 8,
          measure([&](float* left, float* right, std::size_t frames) {
            oneTrack.looper.processBlock(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      {
        LooperFixture fourTracks(blockFrames, loopFrames + blockFrames);
        fourTracks.recordMaster(loopFrames);
        for (std::size_t track = 1; track < ardor::kLooperTrackCount; ++track) {
          fourTracks.addFollower(track, loopFrames);
        }
        printResult("processed_stereo_4_play", blockFrames, options.timedBlocks, 32,
          measure([&](float* left, float* right, std::size_t frames) {
            fourTracks.looper.processBlock(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      {
        LooperFixture overdub(blockFrames, measuredFrames + blockFrames);
        overdub.recordMaster(measuredFrames);
        overdub.command(ardor::LooperCommandType::RecordOrOverdub, 0);
        bool started = false;
        for (std::size_t block = 0; block < measuredFrames / blockFrames + 2; ++block) {
          overdub.process();
          if (overdub.telemetry().tracks[0].state == ardor::LooperTrackState::Overdubbing) {
            started = true;
            break;
          }
        }
        if (!started) throw std::runtime_error("overdub did not start during benchmark setup");
        printResult("processed_stereo_overdub", blockFrames, options.timedBlocks, 32,
          measure([&](float* left, float* right, std::size_t frames) {
            overdub.looper.processBlock(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      {
        DryMonoReference dry(loopFrames, ardor::kLooperTrackCount);
        printResult("dry_mono_4_play_reference", blockFrames, options.timedBlocks, 16,
          measure([&](float* left, float* right, std::size_t frames) {
            dry.process(left, right, frames);
          }, blockFrames, options.warmupBlocks, options.timedBlocks));
      }
      std::cout.flush();
    }
  } catch (const std::exception& error) {
    std::cerr << "looper-bench: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
