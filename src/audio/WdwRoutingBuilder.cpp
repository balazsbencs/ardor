#include "audio/WdwRoutingBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ardor {

namespace {

const char* laneName(bool wet)
{
  return wet ? "wet" : "dry";
}

bool isTimeBlock(const std::string& type)
{
  return type == "mod" || type == "delay" || type == "reverb" || type == "irreverb"
      || type == "stereo";
}

bool isDryBlock(const std::string& type)
{
  return type == "nam" || type == "cab" || type == "dynamics" || type == "eq"
      || type == "distortion" || type == "wah";
}

bool isWetBlock(const std::string& type)
{
  return type == "nam" || type == "cab" || type == "mod" || type == "delay"
      || type == "reverb" || type == "irreverb" || type == "stereo";
}

bool validateLanePlan(const ChainPlan& plan, bool wet,
                      std::unordered_set<std::string>& blockIds,
                      std::string& error)
{
  const char* label = laneName(wet);
  std::size_t namCount = 0;
  std::size_t cabCount = 0;
  std::size_t namIndex = std::numeric_limits<std::size_t>::max();
  std::size_t cabIndex = std::numeric_limits<std::size_t>::max();
  bool timeSeen = false;

  for (std::size_t index = 0; index < plan.blocks.size(); ++index) {
    const auto& block = plan.blocks[index];
    if (block.id.empty()) {
      error = std::string{"WDW "} + label + " lane contains a block with no ID";
      return false;
    }
    if (!blockIds.insert(block.id).second) {
      error = "WDW routing requires globally unique block IDs: " + block.id;
      return false;
    }
    if (block.type == "dualRig" || block.type == "dualAmp") {
      error = std::string{"WDW "} + label
        + " lane cannot contain a nested split block: " + block.id;
      return false;
    }
    // The legacy plan builder uses Disabled to omit a block from the runtime
    // chain entirely. Omitting the required NAM would silently turn this fixed
    // WDW topology into an unprocessed/direct lane, so reject it instead of
    // counting an absent block toward the topology contract. A cabinet is
    // optional: NAM captures may already contain the cabinet response.
    if (block.status == ChainBlockStatus::Disabled
        && block.type == "nam") {
      error = std::string{"WDW "} + label
        + " lane cannot disable its required " + block.type + " block: " + block.id;
      return false;
    }
    const bool allowed = wet ? isWetBlock(block.type) : isDryBlock(block.type);
    if (!allowed) {
      error = std::string{"WDW "} + label + " lane does not admit " + block.type
        + " block: " + block.id;
      return false;
    }
    if (block.status != ChainBlockStatus::Ready
        && block.status != ChainBlockStatus::Disabled) {
      error = std::string{"WDW "} + label + " lane block is not ready: " + block.id;
      return false;
    }
    if (block.type == "nam" && block.status == ChainBlockStatus::Ready) {
      ++namCount;
      namIndex = index;
    } else if (block.type == "cab" && block.status == ChainBlockStatus::Ready) {
      if (timeSeen) {
        error = std::string{"WDW "} + label
          + " lane requires the cabinet before time-based effects: " + block.id;
        return false;
      }
      ++cabCount;
      cabIndex = index;
    }
    if (block.status == ChainBlockStatus::Ready && isTimeBlock(block.type)) {
      if (namIndex == std::numeric_limits<std::size_t>::max()) {
        error = std::string{"WDW "} + label
          + " lane requires NAM before time-based effects: " + block.id;
        return false;
      }
      timeSeen = true;
    }
  }

  if (namCount != 1) {
    error = std::string{"WDW "} + label + " lane requires exactly one NAM block";
    return false;
  }
  if (cabCount > 1) {
    error = std::string{"WDW "} + label + " lane supports at most one cabinet block";
    return false;
  }
  if (cabIndex != std::numeric_limits<std::size_t>::max() && namIndex > cabIndex) {
    error = std::string{"WDW "} + label + " lane requires NAM before cabinet";
    return false;
  }
  return true;
}

bool validateBuildOptions(const WdwRoutingBuildOptions& options, std::string& error)
{
  if (options.engine.sampleRate != 48000) {
    error = "WDW routing requires a 48000 Hz sample rate";
    return false;
  }
  if (options.engine.blockSize == 0) {
    error = "WDW routing requires a non-zero audio block size";
    return false;
  }
  if (options.program.executor.blockSize != 0
      && options.program.executor.blockSize != options.engine.blockSize) {
    error = "WDW program and engine block sizes must match";
    return false;
  }
  if (options.program.executor.sampleRate > 0.0
      && std::fabs(options.program.executor.sampleRate
                   - static_cast<double>(options.engine.sampleRate)) > 0.001) {
    error = "WDW program and engine sample rates must match";
    return false;
  }
  if (!std::isfinite(options.calibrationThreshold) || options.calibrationThreshold <= 0.0f) {
    error = "WDW latency calibration threshold must be finite and greater than zero";
    return false;
  }
  if (options.calibrateLatencies && options.calibrationBlocks == 0) {
    error = "WDW latency calibration requires at least one probe block";
    return false;
  }
  if (options.program.executor.mode == WdwPairExecutionMode::Pipelined) {
    if (options.dryWorkerCpu >= 0 && options.dryWorkerCpu == options.wetWorkerCpu) {
      error = "WDW dry and wet workers must use distinct CPUs";
      return false;
    }
    if (options.program.audioCpu >= 0
        && (options.dryWorkerCpu == options.program.audioCpu
            || options.wetWorkerCpu == options.program.audioCpu)) {
      error = "WDW worker CPU cannot equal the audio CPU";
      return false;
    }
    if (options.program.executor.requireAffinity
        && (options.dryWorkerCpu < 0 || options.wetWorkerCpu < 0)) {
      error = "WDW pipelined admission requires two pinned worker CPUs";
      return false;
    }
  }
  return true;
}

bool calibrateFirstArrival(RuntimeChain& chain, std::size_t blockSize,
                           std::size_t probeBlocks, float threshold,
                           std::size_t& firstArrival, std::string& error)
{
  if (probeBlocks > (std::numeric_limits<std::size_t>::max() / blockSize)) {
    error = "WDW latency probe exceeds addressable frame count";
    return false;
  }

  std::vector<float> input(blockSize, 0.0f);
  std::vector<float> left(blockSize, 0.0f);
  std::vector<float> right(blockSize, 0.0f);
  input[0] = 1.0f;
  chain.reset();
  const std::uint64_t initialFaults = chain.nonFiniteBlockCount();
  const std::size_t maximumFrames = probeBlocks * blockSize;
  for (std::size_t block = 0; block < probeBlocks; ++block) {
    chain.processBlock(input.data(), left.data(), right.data(), blockSize);
    input[0] = 0.0f;
    for (std::size_t frame = 0; frame < blockSize; ++frame) {
      if (!std::isfinite(left[frame]) || !std::isfinite(right[frame])) {
        error = "WDW latency probe produced non-finite audio";
        chain.reset();
        return false;
      }
      if (std::max(std::fabs(left[frame]), std::fabs(right[frame])) >= threshold) {
        firstArrival = block * blockSize + frame;
        if (chain.nonFiniteBlockCount() != initialFaults) {
          error = "WDW latency probe observed a non-finite block";
          chain.reset();
          return false;
        }
        chain.reset();
        return true;
      }
    }
  }
  chain.reset();
  error = "WDW latency probe found no signal in " + std::to_string(maximumFrames)
    + " frames";
  return false;
}

} // namespace

bool buildWdwRoutingProgram(const ChainPlan& dryPlan, const ChainPlan& wetPlan,
                            const WdwRoutingBuildOptions& options,
                            std::unique_ptr<WdwRoutingProgram>& program,
                            WdwRoutingBuildReport& report, std::string& error)
{
  program.reset();
  report = {};
  error.clear();
  if (!validateBuildOptions(options, error)) return false;

  std::unordered_set<std::string> blockIds;
  if (!validateLanePlan(dryPlan, false, blockIds, error)
      || !validateLanePlan(wetPlan, true, blockIds, error)) {
    return false;
  }

  auto dryChain = std::make_unique<RuntimeChain>();
  auto wetChain = std::make_unique<RuntimeChain>();
  if (!prepareRuntimeChain(*dryChain, dryPlan.blocks, options.engine, error)) {
    return false;
  }
  if (!prepareRuntimeChain(*wetChain, wetPlan.blocks, options.engine, error)) {
    return false;
  }

  auto programOptions = options.program;
  programOptions.executor.blockSize = options.engine.blockSize;
  programOptions.executor.sampleRate = static_cast<double>(options.engine.sampleRate);
  programOptions.audioCpu = options.program.audioCpu;

  if (options.calibrateLatencies) {
    if (!calibrateFirstArrival(*dryChain, options.engine.blockSize,
                               options.calibrationBlocks, options.calibrationThreshold,
                               report.dryLatencyFrames, error)
        || !calibrateFirstArrival(*wetChain, options.engine.blockSize,
                                  options.calibrationBlocks, options.calibrationThreshold,
                                  report.wetLatencyFrames, error)) {
      return false;
    }
    report.latencyCalibrated = true;
    programOptions.dryLatencyFrames = report.dryLatencyFrames;
    programOptions.wetLatencyFrames = report.wetLatencyFrames;
  } else {
    report.dryLatencyFrames = programOptions.dryLatencyFrames;
    report.wetLatencyFrames = programOptions.wetLatencyFrames;
  }
  report.totalLatencyFrames = std::max(report.dryLatencyFrames, report.wetLatencyFrames)
    + (programOptions.executor.mode == WdwPairExecutionMode::Pipelined
       ? options.engine.blockSize : 0);

  auto nextProgram = std::make_unique<WdwRoutingProgram>();
  if (!nextProgram->prepare(
        {"dry", std::move(dryChain), options.dryWorkerCpu},
        {"wet", std::move(wetChain), options.wetWorkerCpu},
        programOptions, error)) {
    return false;
  }
  program = std::move(nextProgram);
  return true;
}

bool applyWdwRouting(PedalEngine& engine, const ChainPlan& dryPlan,
                     const ChainPlan& wetPlan,
                     const WdwRoutingBuildOptions& options,
                     WdwRoutingBuildReport& report, std::string& error)
{
  if (std::fabs(dryPlan.inputGain - wetPlan.inputGain) > 1.0e-6f
      || std::fabs(dryPlan.outputGain - wetPlan.outputGain) > 1.0e-6f
      || std::fabs(dryPlan.safetyLimit - wetPlan.safetyLimit) > 1.0e-6f) {
    error = "WDW lanes must share global input/output gain and safety settings";
    return false;
  }

  std::unique_ptr<WdwRoutingProgram> program;
  if (!buildWdwRoutingProgram(dryPlan, wetPlan, options, program, report, error)) {
    return false;
  }

  PedalEngine prepared;
  prepared.setSampleRate(options.engine.sampleRate);
  prepared.prepareBlockSize(options.engine.blockSize);
  prepared.setInputGain(dryPlan.inputGain);
  prepared.setOutputGain(dryPlan.outputGain);
  prepared.setSafetyLimit(dryPlan.safetyLimit);
  prepared.setSafetyLimiterEnabled(true);
  if (!prepared.installPreparedWdwRouting(std::move(program), error)) {
    return false;
  }
  engine.replacePreparedProgram(std::move(prepared));
  return true;
}

} // namespace ardor
