#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"

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
}
