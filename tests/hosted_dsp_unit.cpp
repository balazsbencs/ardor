#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/delay_line_sdram.h"
#include "daisyfx/hosted/dsp/fast_math.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "daisyfx/hosted/dsp/tone_filter.h"
#include "daisyfx/hosted/params/reverb_param_map.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireFinite(float value, const std::string& message)
{
  require(std::isfinite(value), message);
}

void verifyToneFilter(float sampleRate)
{
  pedal::ToneFilter tone;
  tone.Init(sampleRate);

  for (int i = 0; i <= 1000; ++i) {
    const float knob = static_cast<float>(i) / 1000.0f;
    tone.SetKnob(knob);
    for (int sample = 0; sample < 1024; ++sample) {
      const float input = sample == 0 ? 1.0f : ((sample & 1) == 0 ? 0.25f : -0.25f);
      requireFinite(tone.Process(input), "ToneFilter sweep must remain finite");
    }
  }

  tone.SetKnob(0.5f);
  const float inputs[] = {-1.0f, -0.25f, 0.0f, 0.125f, 1.0f};
  for (const float input : inputs) {
    require(tone.Process(input) == input, "ToneFilter centre must be a true bypass");
  }

  tone.SetKnob(std::numeric_limits<float>::quiet_NaN());
  require(tone.Process(0.25f) == 0.25f, "non-finite ToneFilter knob must fall back to centre");
  require(tone.Process(std::numeric_limits<float>::infinity()) == 0.0f,
          "non-finite ToneFilter input must be contained");
  require(tone.Process(0.0f) == 0.0f, "ToneFilter must recover after non-finite input");

  tone.SetKnob(0.0f);
  (void)tone.Process(1.0f);
  tone.Reset();
  require(tone.Process(0.0f) == 0.0f, "ToneFilter reset must clear state");
}

void verifyHalfbandResamplers()
{
  pedal::HalfbandDecimator2x decimator;
  float output = 0.0f;
  for (int sample = 0; sample < 256; ++sample) {
    if (decimator.Push(1.0f, output)) {
      requireFinite(output, "halfband decimator output must remain finite");
    }
  }
  require(std::fabs(output - 1.0f) < 0.001f, "halfband decimator must preserve DC gain");

  pedal::HalfbandInterpolator2x interpolator;
  std::array<float, 2> upsampled{};
  for (int sample = 0; sample < 256; ++sample) {
    upsampled = interpolator.Process(1.0f);
    requireFinite(upsampled[0], "halfband interpolator output must remain finite");
    requireFinite(upsampled[1], "halfband interpolator output must remain finite");
  }
  require(std::fabs(upsampled[0] - 1.0f) < 0.003f && std::fabs(upsampled[1] - 1.0f) < 0.003f,
          "halfband interpolator must preserve DC gain");

  decimator.Reset();
  interpolator.Reset();
  std::array<float, 2> cached{};
  std::size_t cachedCount = 0;
  std::size_t cachedIndex = 0;
  float peak = 0.0f;
  std::size_t peakFrame = 0;
  for (std::size_t frame = 0; frame < 96; ++frame) {
    float sample = 0.0f;
    if (cachedCount != 0U) {
      sample = cached[cachedIndex++];
      --cachedCount;
    }
    float downsampled = 0.0f;
    if (decimator.Push(frame == 0U ? 1.0f : 0.0f, downsampled)) {
      cached = interpolator.Process(downsampled);
      cachedIndex = 0;
      cachedCount = cached.size();
    }
    if (std::fabs(sample) > peak) {
      peak = std::fabs(sample);
      peakFrame = frame;
    }
  }
  require(peakFrame == 31U, "halfband adapter latency must match the dry delay");
}

void verifyFastSineAccuracy()
{
  constexpr float kTwoPi = 6.28318530718f;
  float maxError = 0.0f;
  for (int index = 0; index <= 10000; ++index) {
    const float phase = kTwoPi * static_cast<float>(index) / 10000.0f;
    const float approximation = pedal::fast_sin(phase);
    require(approximation >= -1.0f && approximation <= 1.0f,
            "fast sine must not overshoot its normalized range");
    maxError = std::max(maxError, std::fabs(approximation - std::sin(phase)));
  }
  require(maxError < 0.0002f, "fast sine must meet the modulation accuracy target");
}

void verifyBrightReverbs()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "reverb") continue;

    ardor::DaisyFxProcessor processor;
    std::string error;
    auto params = ardor::defaultDaisyFxParams(descriptor);
    params["mix"] = 1.0f;
    params["tone"] = 1.0f;
    require(processor.configure(descriptor.blockType, params, 48000.0f, error), error);

    for (int sample = 0; sample < 96000; ++sample) {
      const float input = sample == 0 ? 1.0f : 0.0f;
      const auto output = processor.process({input, input});
      requireFinite(output.left, descriptor.mode + " bright reverb left must remain finite");
      requireFinite(output.right, descriptor.mode + " bright reverb right must remain finite");
    }
  }
}

void verifyDelayLineReset()
{
  std::array<float, 64> buffer{};
  pedal::DelayLineSdram line;
  line.Init(buffer.data(), buffer.size());
  line.SetDelay(17.0f);
  line.Reset();

  for (int sample = 0; sample < 32; ++sample) {
    const float output = line.Read();
    if (sample == 17) {
      require(output == 1.0f, "DelayLine reset must retain the configured delay");
    } else {
      require(output == 0.0f, "DelayLine reset must clear prior audio history");
    }
    line.Write(sample == 0 ? 1.0f : 0.0f);
  }
}

void verifyDelayStartup()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "delay") continue;

    ardor::DaisyFxProcessor processor;
    std::string error;
    auto params = ardor::defaultDaisyFxParams(descriptor);
    params["time"] = 1.0f;
    params["repeats"] = 0.0f;
    params["mix"] = 1.0f;
    require(processor.configure(descriptor.blockType, params, 48000.0f, error), error);

    float peak = 0.0f;
    for (int sample = 0; sample < 1024; ++sample) {
      const float input = sample == 0 ? 1.0f : 0.0f;
      const auto output = processor.process({input, input});
      requireFinite(output.left, descriptor.mode + " delay left must remain finite");
      requireFinite(output.right, descriptor.mode + " delay right must remain finite");
      peak = std::max(peak, std::max(std::fabs(output.left), std::fabs(output.right)));
    }
    require(peak < 0.000001f, descriptor.mode + " delay must not start at a two-sample tap");
  }
}

void verifyPhysicalReverbMappings()
{
  using pedal::ReverbModeId;
  using pedal::map_param;
  using pedal::reverb_fx::ParamId;
  using pedal::reverb_fx::get_param_range;

  const auto bloomTime = map_param(0.5f, get_param_range(ReverbModeId::Bloom, ParamId::Param1));
  const auto bloomFeedback = map_param(1.0f, get_param_range(ReverbModeId::Bloom, ParamId::Param2));
  const auto swellRise = map_param(1.0f, get_param_range(ReverbModeId::Swell, ParamId::Param1));
  const auto choraleVowel = map_param(1.0f, get_param_range(ReverbModeId::Chorale, ParamId::Param1));
  const auto magnetoHeads = map_param(0.0f, get_param_range(ReverbModeId::Magneto, ParamId::Param1));

  require(std::fabs(bloomTime - 1.625f) < 0.000001f, "Bloom time map is physical seconds");
  require(std::fabs(bloomFeedback - 0.7f) < 0.000001f, "Bloom feedback map is physical gain");
  require(std::fabs(swellRise - 4.0f) < 0.000001f, "Swell rise map is physical seconds");
  require(std::fabs(choraleVowel - 6.0f) < 0.000001f, "Chorale vowel map is a physical index");
  require(std::fabs(magnetoHeads - 1.0f) < 0.000001f, "Magneto head map is a physical selector");
}

void verifyReverbReset()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", "room");
  require(descriptor != nullptr, "room reverb descriptor exists");

  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 1.0f;
  params["mod"] = 1.0f;

  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", params, 48000.0f, error), error);

  const auto render = [&processor]() {
    std::array<ardor::StereoSample, 1024> output{};
    for (std::size_t i = 0; i < output.size(); ++i) {
      const float input = i == 0 ? 1.0f : 0.0f;
      output[i] = processor.process({input, input});
    }
    return output;
  };

  processor.reset();
  const auto first = render();
  processor.reset();
  const auto second = render();
  for (std::size_t i = 0; i < first.size(); ++i) {
    require(first[i].left == second[i].left && first[i].right == second[i].right,
            "reverb reset must restore deterministic FDN modulation state");
  }
}

void verifyReverbLatency()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", "room");
  require(descriptor != nullptr, "room reverb descriptor exists");

  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 0.0f;
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", params, 48000.0f, error), error);
  require(processor.latencyFrames() == 31U, "reverb boundary reports its fixed latency");

  for (std::size_t sample = 0; sample <= processor.latencyFrames(); ++sample) {
    const auto output = processor.process({sample == 0 ? 1.0f : 0.0f, 0.0f});
    const float expected = sample == processor.latencyFrames() ? 1.0f : 0.0f;
    require(output.left == expected && output.right == 0.0f,
            "dry reverb path must match the resampler latency exactly");
  }
}

void verifyOutputMixSmoothing()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "flanger");
  require(descriptor != nullptr, "flanger descriptor exists");
  auto dryParams = ardor::defaultDaisyFxParams(*descriptor);
  dryParams["mix"] = 0.0f;
  auto wetParams = dryParams;
  wetParams["mix"] = 1.0f;

  ardor::DaisyFxProcessor ramped;
  ardor::DaisyFxProcessor fullyWet;
  std::string error;
  require(ramped.configure("mod", dryParams, 48000.0f, error), error);
  require(fullyWet.configure("mod", wetParams, 48000.0f, error), error);

  for (int frame = 0; frame < 48; ++frame) {
    const float input = std::sin(static_cast<float>(frame) * 0.11f);
    (void)ramped.process({input, input});
    (void)fullyWet.process({input, input});
  }
  require(ramped.setParameterTarget("mix", 1.0f), "mix target must be accepted");

  const float input = std::sin(48.0f * 0.11f);
  const auto rampedOutput = ramped.process({input, input});
  const auto wetOutput = fullyWet.process({input, input});
  const float rampedDelta = std::fabs(rampedOutput.left - input);
  const float wetDelta = std::fabs(wetOutput.left - input);
  require(rampedDelta < wetDelta * 0.05f + 0.00001f,
          "mix automation must ramp rather than step to the wet signal");
}

} // namespace

int main()
{
  verifyToneFilter(24000.0f);
  verifyToneFilter(48000.0f);
  verifyHalfbandResamplers();
  verifyFastSineAccuracy();
  verifyBrightReverbs();
  verifyDelayLineReset();
  verifyDelayStartup();
  verifyPhysicalReverbMappings();
  verifyReverbReset();
  verifyReverbLatency();
  verifyOutputMixSmoothing();
}
