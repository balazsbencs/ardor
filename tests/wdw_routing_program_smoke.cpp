#include "dsp/WdwRoutingProgram.h"
#include "rat/RatProcessor.h"
#include "tape/TapeProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr float kSqrtHalf = 0.7071067811865475f;
constexpr float kQuarterPi = 0.78539816339744830962f;
constexpr std::size_t kReferenceFrames = 8;

struct ReferenceOutput {
  std::vector<float> left;
  std::vector<float> right;
};

std::unique_ptr<ardor::RuntimeChain> emptyChain()
{
  return std::make_unique<ardor::RuntimeChain>();
}

std::unique_ptr<ardor::RuntimeChain> referenceChain(std::string id = "reference-cab")
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  chain->addCab({1.0f, 0.31f, -0.17f, 0.08f}, 0.83f, 1.0f, std::move(id));
  chain->prepareBlockSize(kReferenceFrames);
  return chain;
}

std::unique_ptr<ardor::RuntimeChain> liveControlDryChain(std::string& error)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  ardor::RatProcessor rat;
  if (!rat.configure({{"mode", "rat"}}, 48000.0f, error)) return {};
  chain->addDistortion("dry-rat", std::move(rat));

  ardor::TapeProcessor tape;
  if (!tape.configure({{"mode", "tape"}}, 48000.0f, error)) return {};
  chain->addDistortion("dry-tape", std::move(tape));
  chain->addCab({1.0f, 0.25f}, 1.0f, 1.0f, "dry-cab");
  chain->prepareBlockSize(kReferenceFrames);
  return chain;
}

std::unique_ptr<ardor::RuntimeChain> liveControlWetChain(std::string& error)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  chain->addCab({1.0f, 0.25f}, 1.0f, 1.0f, "wet-cab");
  std::vector<float> impulse(2048, 0.0f);
  impulse[0] = 1.0f;
  impulse[511] = 0.2f;
  if (!chain->addIrReverb("wet-ir-reverb", impulse, impulse, 48000.0f, error)) return {};
  if (!chain->addStereoWidener("wet-stereo", 48000.0f, error)) return {};
  chain->prepareBlockSize(kReferenceFrames);
  return chain;
}

ardor::WdwRoutingProgramOptions directOptions()
{
  ardor::WdwRoutingProgramOptions options;
  options.executor.blockSize = 8;
  options.executor.sampleRate = 48000.0;
  options.executor.mode = ardor::WdwPairExecutionMode::Direct;
  options.executor.requireWorkerSetup = false;
  options.executor.requireRealtimeScheduling = false;
  options.executor.requireAffinity = false;
  options.mix = {1.0f, 0.0f, true, 1.0f, 1.0f, true};
  return options;
}

bool near(float actual, float expected, float tolerance = 1.0e-5f)
{
  return std::fabs(actual - expected) <= tolerance;
}

bool require(bool condition, const char* message)
{
  if (!condition) std::cerr << "WDW routing program smoke: " << message << '\n';
  return condition;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 8;
  std::vector<float> input(kFrames, 1.0f);
  std::vector<float> left(kFrames, 0.0f);
  std::vector<float> right(kFrames, 0.0f);
  std::string error;

  ardor::WdwRoutingProgram program;
  auto options = directOptions();
  options.dryLatencyFrames = 0;
  options.wetLatencyFrames = 2;
  if (!require(program.prepare({"dry", emptyChain(), -1},
                               {"wet", emptyChain(), -1}, options, error),
               error.c_str())) return 1;
  if (!require(program.prepared() && program.blockSize() == kFrames
                 && program.latencyFrames() == 2
                 && program.alignmentDelayFrames(0) == 2
                 && program.alignmentDelayFrames(1) == 0,
               "latency metadata or alignment was not prepared")) return 1;

  ardor::WdwRoutingProcessResult result;
  if (!require(program.processBlock(input.data(), left.data(), right.data(),
                                    kFrames, result),
               "direct WDW program rejected a valid block")) return 1;
  if (!require(result.accepted && result.outputReady && result.pairReady,
               "direct WDW result flags were incorrect")) return 1;
  if (!require(near(left[0], 1.0f) && near(right[0], 1.0f)
                 && near(left[1], 1.0f) && near(right[1], 1.0f),
               "wet lane should arrive before the aligned dry lane")) return 1;
  if (!require(near(left[2], 1.0f + kSqrtHalf)
                 && near(right[2], 1.0f + kSqrtHalf),
               "dry and wet lanes were not aligned and mixed")) return 1;

  ardor::WdwRoutingProgram laneControls;
  auto controlOptions = directOptions();
  if (!require(laneControls.prepare({"dry", referenceChain("dry-cab"), -1},
                                    {"wet", referenceChain("wet-cab"), -1},
                                    controlOptions, error),
               error.c_str())) return 1;
  if (!require(laneControls.setBlockEnabled("dry-cab", false),
               "WDW block-enable control did not reach a lane chain")) return 1;
  if (!require(laneControls.setCabParameter("wet-cab", "levelDb", -60.0f)
                 && !laneControls.setCabParameter("missing-cab", "mix", 0.5f),
               "ID-scoped WDW cabinet control was not routed correctly")) return 1;
  if (!require(laneControls.setMix({0.0f, 0.0f, false, 1.0f, 1.0f, true}),
               "cabinet control test could not isolate the wet lane")) return 1;
  ardor::WdwRoutingProcessResult laneControlResult;
  for (std::size_t block = 0; block < 2000; ++block) {
    if (!require(laneControls.processBlock(input.data(), left.data(), right.data(),
                                           kFrames, laneControlResult),
                 "cabinet control test could not process WDW audio")) return 1;
  }
  if (!require(std::fabs(left.back()) < 0.01f && std::fabs(right.back()) < 0.01f,
               "lane-scoped cabinet level did not change the wet lane output")) return 1;

  if (!require(!program.setMix({std::numeric_limits<float>::quiet_NaN(), 0.0f,
                                true, 1.0f, 1.0f, true}),
               "non-finite mix target was accepted")) return 1;
  if (!require(program.setMix({1.0f, -1.0f, true, 0.0f, 1.0f, false}),
               "valid dry-pan mix target was rejected")) return 1;
  for (std::size_t block = 0; block < 64; ++block) {
    if (!require(program.processBlock(input.data(), left.data(), right.data(),
                                      kFrames, result),
                 "program rejected a block during mix smoothing")) return 1;
  }
  if (!require(left.back() > 0.99f && std::fabs(right.back()) < 0.01f,
               "dry pan and wet disable target did not settle")) return 1;

  program.reset();
  if (!require(program.processBlock(input.data(), left.data(), right.data(),
                                    kFrames, result),
               "program rejected a block after reset")) return 1;
  if (!require(left[0] < 0.01f && std::fabs(right[0]) < 0.01f,
               "reset did not clear the fixed alignment delay")) return 1;

  ardor::WdwRoutingProgram invalidWorkers;
  auto invalidOptions = directOptions();
  invalidOptions.executor.mode = ardor::WdwPairExecutionMode::Pipelined;
  invalidOptions.audioCpu = 2;
  if (!require(!invalidWorkers.prepare({"dry", emptyChain(), 3},
                                       {"wet", emptyChain(), 3},
                                       invalidOptions, error),
               "worker CPU collision was accepted")) return 1;

  ardor::WdwRoutingProgram pipelined;
  auto pipelineOptions = directOptions();
  pipelineOptions.executor.mode = ardor::WdwPairExecutionMode::Pipelined;
  pipelineOptions.executor.requireWorkerSetup = false;
  pipelineOptions.executor.requireRealtimeScheduling = false;
  pipelineOptions.executor.requireAffinity = false;
  pipelineOptions.mix = {0.0f, 0.0f, false, 1.0f, 1.0f, true};
  if (!require(pipelined.prepare({"dry", emptyChain(), -1},
                                 {"wet", emptyChain(), -1},
                                 pipelineOptions, error),
               error.c_str())) return 1;
  bool sawPair = false;
  for (std::size_t block = 0; block < 100; ++block) {
    if (!require(pipelined.processBlock(input.data(), left.data(), right.data(),
                                        kFrames, result),
                 "pipelined WDW program rejected a valid block")) return 1;
    if (!result.outputReady) {
      for (float value : left) {
        if (!near(value, 0.0f)) return 1;
      }
      for (float value : right) {
        if (!near(value, 0.0f)) return 1;
      }
    }
    if (result.pairReady) {
      sawPair = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!require(sawPair, "pipelined WDW program never published a pair")) return 1;

  // Live controls are written by the management thread while the two lane
  // chains are owned by the executor workers. This loop is intentionally part
  // of the TSAN subset: it catches processors that accidentally mutate their
  // audio state directly from a setter.
  ardor::WdwRoutingProgram concurrentControls;
  auto concurrentOptions = directOptions();
  concurrentOptions.executor.mode = ardor::WdwPairExecutionMode::Pipelined;
  concurrentOptions.executor.requireWorkerSetup = false;
  concurrentOptions.executor.requireRealtimeScheduling = false;
  concurrentOptions.executor.requireAffinity = false;
  auto liveDry = liveControlDryChain(error);
  auto liveWet = liveControlWetChain(error);
  if (!require(liveDry && liveWet, error.c_str())) return 1;
  if (!require(concurrentControls.prepare({"dry", std::move(liveDry), -1},
                                          {"wet", std::move(liveWet), -1},
                                          concurrentOptions, error),
               error.c_str())) return 1;
  std::atomic<bool> controlStarted{false};
  std::atomic<bool> keepControlling{true};
  std::thread controlThread([&] {
    controlStarted.store(true, std::memory_order_release);
    std::size_t step = 0;
    while (keepControlling.load(std::memory_order_acquire)) {
      const float unit = static_cast<float>(step % 101) / 100.0f;
      (void)concurrentControls.setDistortionParameter("dry-rat", "distortion", unit);
      (void)concurrentControls.setDistortionParameter("dry-tape", "saturation", unit);
      (void)concurrentControls.setDistortionParameter("dry-tape", "bias", 1.0f - unit);
      (void)concurrentControls.setDistortionParameter("dry-tape", "flutter", unit);
      (void)concurrentControls.setIrReverbParameter("wet-ir-reverb", "mix", unit);
      (void)concurrentControls.setIrReverbParameter(
        "wet-ir-reverb", "lowCutHz", 20.0f + unit * 1980.0f);
      (void)concurrentControls.setStereoWidenerParameter("wet-stereo", "width", unit * 2.0f);
      (void)concurrentControls.setStereoWidenerParameter(
        "wet-stereo", "bassMonoHz", unit * 500.0f);
      (void)concurrentControls.setCabParameter("dry-cab", "mix", unit);
      (void)concurrentControls.setCabParameter("wet-cab", "levelDb", -24.0f + unit * 24.0f);
      ++step;
    }
  });
  while (!controlStarted.load(std::memory_order_acquire)) std::this_thread::yield();
  bool concurrentOutputFinite = true;
  for (std::size_t block = 0; block < 400; ++block) {
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
      input[frame] = 0.05f * std::sin(static_cast<float>(block * kFrames + frame) * 0.03f);
    }
    if (!concurrentControls.processBlock(input.data(), left.data(), right.data(), kFrames, result)) {
      concurrentOutputFinite = false;
      break;
    }
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
      concurrentOutputFinite = concurrentOutputFinite
        && std::isfinite(left[frame]) && std::isfinite(right[frame]);
    }
  }
  keepControlling.store(false, std::memory_order_release);
  controlThread.join();
  if (!require(concurrentOutputFinite,
               "concurrent WDW parameter changes produced invalid output")) return 1;

  // Compare the complete direct program against two independently processed
  // serial chains. This is the reference for the final mixer and fixed path
  // alignment; it must not depend on the executor's internal buffers.
  auto referenceOptions = directOptions();
  referenceOptions.mix = {0.73f, -0.35f, true, 0.47f, 0.42f, true};
  ardor::WdwRoutingProgram directCompared;
  if (!require(directCompared.prepare({"dry", referenceChain(), -1},
                                      {"wet", referenceChain(), -1},
                                      referenceOptions, error),
               error.c_str())) return 1;
  auto serialDry = referenceChain();
  auto serialWet = referenceChain();
  std::vector<float> serialDryLeft(kReferenceFrames, 0.0f);
  std::vector<float> serialDryRight(kReferenceFrames, 0.0f);
  std::vector<float> serialWetLeft(kReferenceFrames, 0.0f);
  std::vector<float> serialWetRight(kReferenceFrames, 0.0f);
  std::vector<float> comparedInput(kReferenceFrames, 0.0f);
  std::vector<float> comparedLeft(kReferenceFrames, 0.0f);
  std::vector<float> comparedRight(kReferenceFrames, 0.0f);
  const float dryAngle = (referenceOptions.mix.dryPan + 1.0f) * kQuarterPi;
  const float dryLeftGain = referenceOptions.mix.dryLevel * std::cos(dryAngle);
  const float dryRightGain = referenceOptions.mix.dryLevel * std::sin(dryAngle);
  for (std::size_t block = 0; block < 8; ++block) {
    for (std::size_t frame = 0; frame < kReferenceFrames; ++frame) {
      comparedInput[frame] = 0.015f * static_cast<float>(block + 1)
        + 0.002f * static_cast<float>(frame);
    }
    if (!require(directCompared.processBlock(comparedInput.data(), comparedLeft.data(),
                                             comparedRight.data(), kReferenceFrames, result),
                 "direct program rejected the serial-reference block")) return 1;
    serialDry->processBlock(comparedInput.data(), serialDryLeft.data(),
                            serialDryRight.data(), kReferenceFrames);
    serialWet->processBlock(comparedInput.data(), serialWetLeft.data(),
                             serialWetRight.data(), kReferenceFrames);
    for (std::size_t frame = 0; frame < kReferenceFrames; ++frame) {
      const float dryMono = (serialDryLeft[frame] + serialDryRight[frame]) * 0.5f;
      const float wetMid = (serialWetLeft[frame] + serialWetRight[frame]) * 0.5f;
      const float wetSide = (serialWetLeft[frame] - serialWetRight[frame])
        * 0.5f * referenceOptions.mix.wetWidth;
      const float expectedLeft = dryMono * dryLeftGain
        + (wetMid + wetSide) * referenceOptions.mix.wetLevel;
      const float expectedRight = dryMono * dryRightGain
        + (wetMid - wetSide) * referenceOptions.mix.wetLevel;
      if (!require(near(comparedLeft[frame], expectedLeft, 2.0e-5f)
                     && near(comparedRight[frame], expectedRight, 2.0e-5f),
                   "direct program diverged from the serial reference")) return 1;
    }
  }

  // The pipelined program may publish an older generation, but every
  // generation it does publish must equal the corresponding serial result.
  auto pipelinedReferenceOptions = referenceOptions;
  pipelinedReferenceOptions.executor.mode = ardor::WdwPairExecutionMode::Pipelined;
  pipelinedReferenceOptions.executor.requireWorkerSetup = false;
  pipelinedReferenceOptions.executor.requireRealtimeScheduling = false;
  pipelinedReferenceOptions.executor.requireAffinity = false;
  ardor::WdwRoutingProgram pipelinedCompared;
  if (!require(pipelinedCompared.prepare({"dry", referenceChain(), -1},
                                         {"wet", referenceChain(), -1},
                                         pipelinedReferenceOptions, error),
               error.c_str())) return 1;
  auto pipelinedSerialDry = referenceChain();
  auto pipelinedSerialWet = referenceChain();
  std::vector<ReferenceOutput> serialGenerations;
  serialGenerations.reserve(128);
  std::size_t comparedGenerations = 0;
  bool sawReferencePair = false;
  for (std::size_t block = 0; block < 200 && comparedGenerations < 24; ++block) {
    for (std::size_t frame = 0; frame < kReferenceFrames; ++frame) {
      comparedInput[frame] = 0.011f * static_cast<float>(block + 1)
        + 0.001f * static_cast<float>(frame);
    }
    if (!require(pipelinedCompared.processBlock(comparedInput.data(), comparedLeft.data(),
                                                comparedRight.data(), kReferenceFrames,
                                                result),
                 "pipelined program rejected the serial-reference block")) return 1;
    if (result.accepted) {
      ReferenceOutput expected{
        std::vector<float>(kReferenceFrames, 0.0f),
        std::vector<float>(kReferenceFrames, 0.0f),
      };
      std::vector<float> expectedRightDry(kReferenceFrames, 0.0f);
      std::vector<float> expectedLeftWet(kReferenceFrames, 0.0f);
      std::vector<float> expectedRightWet(kReferenceFrames, 0.0f);
      pipelinedSerialDry->processBlock(comparedInput.data(), expected.left.data(),
                                       expectedRightDry.data(), kReferenceFrames);
      pipelinedSerialWet->processBlock(comparedInput.data(), expectedLeftWet.data(),
                                       expectedRightWet.data(), kReferenceFrames);
      for (std::size_t frame = 0; frame < kReferenceFrames; ++frame) {
        const float dryMono = (expected.left[frame] + expectedRightDry[frame]) * 0.5f;
        const float wetMid = (expectedLeftWet[frame] + expectedRightWet[frame]) * 0.5f;
        const float wetSide = (expectedLeftWet[frame] - expectedRightWet[frame])
          * 0.5f * pipelinedReferenceOptions.mix.wetWidth;
        expected.left[frame] = dryMono * dryLeftGain
          + (wetMid + wetSide) * pipelinedReferenceOptions.mix.wetLevel;
        expected.right[frame] = dryMono * dryRightGain
          + (wetMid - wetSide) * pipelinedReferenceOptions.mix.wetLevel;
      }
      serialGenerations.push_back(std::move(expected));
    }
    if (result.pairReady) {
      if (!require(result.outputGeneration > 0
                     && result.outputGeneration <= serialGenerations.size(),
                   "pipelined output generation had no serial reference")) return 1;
      const auto& expected = serialGenerations[result.outputGeneration - 1];
      for (std::size_t frame = 0; frame < kReferenceFrames; ++frame) {
        if (!require(near(comparedLeft[frame], expected.left[frame], 2.0e-5f)
                       && near(comparedRight[frame], expected.right[frame], 2.0e-5f),
                     "pipelined output diverged from the serial generation")) return 1;
      }
      sawReferencePair = true;
      ++comparedGenerations;
    }
    if (!result.pairReady) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!require(sawReferencePair && comparedGenerations >= 24,
               "pipelined serial-reference comparison did not make progress")) return 1;
  return 0;
}
