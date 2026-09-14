#include "dsp/ParallelLaneMixer.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

bool near(float actual, float expected)
{
  return std::abs(actual - expected) < 1.0e-5f;
}

bool require(bool condition, const char* message)
{
  if (!condition) std::cerr << "parallel lane mixer smoke: " << message << "\n";
  return condition;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 4;
  constexpr float kEqualPower = 0.70710678118f;
  std::vector<float> dry(kFrames, 2.0f);
  std::vector<float> left(kFrames, 0.0f);
  std::vector<float> right(kFrames, 0.0f);
  std::vector<float> laneLeft(kFrames, 1.0f);
  std::vector<float> laneRight(kFrames, 2.0f);
  const float* lanes[] = {laneLeft.data(), laneRight.data()};

  ardor::ParallelLaneMixer mixer;
  if (!require(!mixer.prepare(0, kFrames), "zero-lane prepare unexpectedly succeeded")) return 1;
  if (!require(mixer.prepare(2, kFrames), "valid prepare failed")) return 1;
  if (!require(mixer.laneCount() == 2 && mixer.blockSize() == kFrames,
               "prepared dimensions were incorrect")) return 1;

  if (!require(mixer.setLane(0, {1.0f, -1.0f, true}), "left lane configuration failed")) return 1;
  if (!require(mixer.setLane(1, {1.0f, 1.0f, true}), "right lane configuration failed")) return 1;
  if (!require(!mixer.setLane(2, {}), "out-of-range lane configuration succeeded")) return 1;
  mixer.setDry({0.0f, 0.0f, false});
  if (!require(mixer.processBlock(dry.data(), lanes, true, left.data(), right.data(), kFrames),
               "panned wet block was rejected")) return 1;
  for (std::size_t i = 0; i < kFrames; ++i) {
    if (!require(near(left[i], 1.0f) && near(right[i], 2.0f),
                 "hard-panned lanes were mixed incorrectly")) return 1;
  }

  if (!require(mixer.setLane(0, {1.0f, 0.0f, true}), "centre lane configuration failed")) return 1;
  if (!require(mixer.setLane(1, {0.0f, 0.0f, false}), "disabled lane configuration failed")) return 1;
  if (!require(mixer.processBlock(dry.data(), lanes, true, left.data(), right.data(), kFrames),
               "centre wet block was rejected")) return 1;
  if (!require(near(left[0], kEqualPower) && near(right[0], kEqualPower),
               "equal-power centre pan was incorrect")) return 1;

  mixer.setDry({1.0f, 0.0f, true});
  if (!require(mixer.processBlock(dry.data(), lanes, true, left.data(), right.data(), kFrames),
               "wet/dry block was rejected")) return 1;
  const float expectedWetDry = 3.0f * kEqualPower;
  if (!require(near(left[0], expectedWetDry) && near(right[0], expectedWetDry),
               "wet/dry centre mix was incorrect")) return 1;

  mixer.setUnderflowPolicy(ardor::ParallelLaneUnderflowPolicy::HoldLastWet);
  std::vector<float> newDry(kFrames, 4.0f);
  if (!require(mixer.processBlock(newDry.data(), nullptr, false,
                                  left.data(), right.data(), kFrames),
               "hold-last underflow was rejected")) return 1;
  const float expectedHeld = 4.0f * kEqualPower + kEqualPower;
  if (!require(near(left[0], expectedHeld) && near(right[0], expectedHeld),
               "hold-last wet fallback was incorrect")) return 1;
  if (!require(mixer.underflowBlockCount() == 1, "underflow count was incorrect")) return 1;

  mixer.setUnderflowPolicy(ardor::ParallelLaneUnderflowPolicy::DryFallback);
  if (!require(mixer.processBlock(newDry.data(), nullptr, false,
                                  left.data(), right.data(), kFrames),
               "dry fallback underflow was rejected")) return 1;
  if (!require(near(left[0], 4.0f * kEqualPower)
                 && near(right[0], 4.0f * kEqualPower),
               "dry fallback was incorrect")) return 1;

  mixer.setUnderflowPolicy(ardor::ParallelLaneUnderflowPolicy::Silence);
  if (!require(mixer.processBlock(newDry.data(), nullptr, false,
                                  left.data(), right.data(), kFrames),
               "silence underflow was rejected")) return 1;
  if (!require(near(left[0], 0.0f) && near(right[0], 0.0f),
               "silence fallback was incorrect")) return 1;

  mixer.reset();
  mixer.setUnderflowPolicy(ardor::ParallelLaneUnderflowPolicy::HoldLastWet);
  if (!require(mixer.processBlock(newDry.data(), nullptr, false,
                                  left.data(), right.data(), kFrames),
               "reset underflow was rejected")) return 1;
  if (!require(near(left[0], 4.0f * kEqualPower)
                 && near(right[0], 4.0f * kEqualPower),
               "reset did not clear held wet audio")) return 1;

  return 0;
}
