#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"
#include "equalizer/ParametricEqProcessor.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

float rms(const std::array<float, 64>& samples)
{
  float sum = 0.0f;
  for (const float sample : samples) {
    sum += sample * sample;
  }
  return std::sqrt(sum / static_cast<float>(samples.size()));
}

} // namespace

int main()
{
  const auto defaults = ardor::defaultParametricEqParams();
  require(defaults.bands.size() == 5, "five EQ bands");
  require(defaults.bands[0].frequencyHz == 80.0f, "band 1 default");
  require(defaults.bands[4].frequencyHz == 8000.0f, "band 5 default");

  const nlohmann::json supplied{
    {"mode", "parametric_eq_5"},
    {"bands", nlohmann::json::array({
      {{"enabled", false}, {"frequency_hz", 1.0f}, {"q", 99.0f}, {"gain_db", -99.0f}},
      {{"enabled", "bad"}, {"frequency_hz", std::numeric_limits<float>::infinity()}},
      nlohmann::json::object(), nlohmann::json::object(), nlohmann::json::object(),
      {{"frequency_hz", 16000.0f}}
    })}
  };
  const auto parsed = ardor::parametricEqParamsFromJson(supplied);
  require(!parsed.bands[0].enabled, "valid band enabled value");
  require(parsed.bands[0].frequencyHz == 20.0f, "frequency clamps low");
  require(parsed.bands[0].q == 18.0f, "Q clamps high");
  require(parsed.bands[0].gainDb == -18.0f, "gain clamps low");
  require(parsed.bands[1].enabled, "wrong enabled type uses default");
  require(parsed.bands[1].frequencyHz == 250.0f, "non-finite frequency uses default");

  const auto canonical = ardor::parametricEqParamsToJson(parsed);
  require(canonical.at("bands").size() == 5, "canonical output has five bands");
  require(canonical.at("mode") == "parametric_eq_5", "canonical mode");

  const auto coefficients = ardor::makePeakingEq(48000.0f, 1000.0f, 1.0f, 6.0f);
  require(std::fabs(ardor::biquadMagnitudeDb(coefficients, 1000.0f, 48000.0f) - 6.0f) < 0.01f,
          "center frequency has requested gain");
  const auto neutral = ardor::makePeakingEq(48000.0f, 1000.0f, 1.0f, 0.0f);
  require(std::fabs(ardor::biquadMagnitudeDb(neutral, 20.0f, 48000.0f)) < 0.0001f,
          "zero-gain biquad is neutral");

  ardor::ParametricEqProcessor processor;
  std::string error;
  auto settings = ardor::defaultParametricEqParams();
  settings.bands[2] = {true, 1000.0f, 1.0f, 6.0f};
  require(processor.configure(settings, 48000.0f, error), "processor configures");

  std::array<float, 64> left{};
  std::array<float, 64> right{};
  left[0] = 1.0f;
  right[0] = 0.5f;
  processor.processBlock(left.data(), right.data(), left.data(), right.data(), left.size());
  for (std::size_t i = 0; i < left.size(); ++i) {
    require(std::isfinite(left[i]) && std::isfinite(right[i]), "finite stereo output");
    require(std::fabs(left[i] - right[i] * 2.0f) < 0.0001f, "stereo state remains independent");
  }

  require(processor.setBandTarget(2, {false, 1000.0f, 1.0f, 6.0f}), "valid target accepted");
  require(!processor.setBandTarget(5, settings.bands[0]), "invalid band rejected");
  processor.reset();
  float firstLeft = 1.0f;
  float firstRight = 1.0f;
  processor.process(firstLeft, firstRight);
  processor.reset();
  float repeatedLeft = 1.0f;
  float repeatedRight = 1.0f;
  processor.process(repeatedLeft, repeatedRight);
  require(firstLeft == repeatedLeft && firstRight == repeatedRight, "reset deterministic");

  ardor::ParametricEqProcessor smoothingProcessor;
  auto smoothingSettings = ardor::defaultParametricEqParams();
  smoothingSettings.bands[2] = {true, 1000.0f, 1.0f, 6.0f};
  require(smoothingProcessor.configure(smoothingSettings, 48000.0f, error), "smoothing processor configures");
  std::array<float, 64> sine{};
  std::array<float, 64> sineRight{};
  float phase = 0.0f;
  constexpr float phaseStep = 2.0f * 3.14159265358979323846f * 1000.0f / 48000.0f;
  float boostedRms = 0.0f;
  for (int block = 0; block < 200; ++block) {
    for (std::size_t i = 0; i < sine.size(); ++i) {
      sine[i] = std::sin(phase);
      sineRight[i] = sine[i];
      phase += phaseStep;
    }
    smoothingProcessor.processBlock(sine.data(), sineRight.data(), sine.data(), sineRight.data(), sine.size());
    boostedRms = rms(sine);
  }
  require(boostedRms > 1.2f, "boosted sine reaches requested response");

  require(smoothingProcessor.setBandTarget(2, {false, 1000.0f, 1.0f, 6.0f}),
          "disable target accepted");
  for (std::size_t i = 0; i < sine.size(); ++i) {
    sine[i] = std::sin(phase);
    sineRight[i] = sine[i];
    phase += phaseStep;
  }
  const float firstDisabledDryRms = rms(sine);
  smoothingProcessor.processBlock(sine.data(), sineRight.data(), sine.data(), sineRight.data(), sine.size());
  const float firstDisabledRms = rms(sine);
  require(firstDisabledRms > firstDisabledDryRms * 1.2f,
          "band disable starts a smooth transition");

  float settledRms = firstDisabledRms;
  float settledDryRms = firstDisabledDryRms;
  for (int block = 0; block < 200; ++block) {
    for (std::size_t i = 0; i < sine.size(); ++i) {
      sine[i] = std::sin(phase);
      sineRight[i] = sine[i];
      phase += phaseStep;
    }
    settledDryRms = rms(sine);
    smoothingProcessor.processBlock(sine.data(), sineRight.data(), sine.data(), sineRight.data(), sine.size());
    settledRms = rms(sine);
  }
  require(std::fabs(settledRms - settledDryRms) < 0.01f, "disabled band converges to dry response");

  require(smoothingProcessor.setBandTarget(2, {true, 20000.0f, 18.0f, 18.0f}),
          "extreme target accepted");
  for (int block = 0; block < 200; ++block) {
    for (std::size_t i = 0; i < sine.size(); ++i) {
      sine[i] = std::sin(phase);
      sineRight[i] = sine[i];
      phase += phaseStep;
    }
    smoothingProcessor.processBlock(sine.data(), sineRight.data(), sine.data(), sineRight.data(), sine.size());
    for (const float sample : sine) {
      require(std::isfinite(sample), "extreme EQ output remains finite");
    }
  }
}
