#pragma once

#include "daisyfx/pog3/Pog3Parameters.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/pitch_shifter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Test/benchmark reference only. This is deliberately NOT a factory mode:
// five granular voices cannot provide independent attack or spectral freeze.
namespace pog3_test {

class GranularReference {
public:
  GranularReference() {
    for (std::size_t i = 0; i < voices_.size(); ++i) {
      for (auto& channel : voices_[i]) {
        channel.history.resize(8192);
        channel.shifter.Init(channel.history.data(), channel.history.size(), 48000, 1024);
        channel.shifter.SetShift(ardor::pog3::kVoiceSemitones[i + 1]);
      }
    }
    values_ = ardor::pog3::defaultValues();
  }

  void setValues(const ardor::pog3::Values& values) noexcept { values_ = values; }
  void setWarp(float extent, bool focus) {
    for (std::size_t i = 0; i < voices_.size(); ++i)
      for (auto& channel : voices_[i])
        channel.shifter.SetShift(ardor::pog3::warpSemitones(i + 1, extent, focus));
  }
  void reset() {
    for (auto& voice : voices_) for (auto& channel : voice) channel.shifter.Reset();
  }
  ardor::StereoSample process(ardor::StereoSample input) {
    using namespace ardor::pog3;
    const float gain = physicalValue(Parameter::InputGain, values_[index(Parameter::InputGain)]);
    input.left *= gain;
    input.right *= gain;
    auto output = pan(input, values_[index(Parameter::DryPan)], values_[index(Parameter::DryLevel)]);
    for (std::size_t i = 0; i < voices_.size(); ++i) {
      const ardor::StereoSample shifted{
        voices_[i][0].shifter.Process(input.left), voices_[i][1].shifter.Process(input.right)};
      const auto contribution = pan(shifted, values_[index(Parameter::Down2Pan) + i],
                                    values_[index(Parameter::Down2Level) + i]);
      output.left += contribution.left;
      output.right += contribution.right;
    }
    const float master = physicalValue(Parameter::MasterLevel, values_[index(Parameter::MasterLevel)]);
    output.left *= master;
    output.right *= master;
    return output;
  }

private:
  static ardor::StereoSample pan(ardor::StereoSample input, float normalized, float level) {
    if (normalized == .5f) return {input.left * level, input.right * level};
    const float angle = std::clamp(normalized, 0.0f, 1.0f) * 1.570796326795f;
    const float left = normalized == 1 ? 0 : 1.41421356237f * std::cos(angle);
    const float right = normalized == 0 ? 0 : 1.41421356237f * std::sin(angle);
    const float mid = (input.left + input.right) * .5f;
    const float side = (input.left - input.right) * .5f * std::min(left, right);
    return {(left * mid + side) * level, (right * mid - side) * level};
  }
  struct Channel {
    std::vector<float> history;
    pedal::PitchShifter shifter;
  };
  std::array<std::array<Channel, 2>, 5> voices_;
  ardor::pog3::Values values_;
};

} // namespace pog3_test
