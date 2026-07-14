#include "equalizer/EqParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ardor {

namespace {

constexpr std::array kDefaultFrequencies = {80.0f, 250.0f, 800.0f, 2500.0f, 8000.0f};

float finiteClamped(const nlohmann::json& object, const char* key, float fallback,
                    float minimum, float maximum)
{
  const auto value = object.find(key);
  if (value == object.end() || !value->is_number()) {
    return fallback;
  }

  const float number = value->get<float>();
  return std::isfinite(number) ? std::clamp(number, minimum, maximum) : fallback;
}

EqBandParams normalized(EqBandParams band)
{
  if (!std::isfinite(band.frequencyHz)) {
    band.frequencyHz = 1000.0f;
  }
  if (!std::isfinite(band.q)) {
    band.q = 1.0f;
  }
  if (!std::isfinite(band.gainDb)) {
    band.gainDb = 0.0f;
  }
  band.frequencyHz = std::clamp(band.frequencyHz, kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
  band.q = std::clamp(band.q, kEqMinimumQ, kEqMaximumQ);
  band.gainDb = std::clamp(band.gainDb, kEqMinimumGainDb, kEqMaximumGainDb);
  return band;
}

} // namespace

EqBandParams defaultParametricEqBand(std::size_t index)
{
  return {true, kDefaultFrequencies[std::min(index, kDefaultFrequencies.size() - 1)], 1.0f, 0.0f};
}

ParametricEqParams defaultParametricEqParams()
{
  ParametricEqParams result;
  for (std::size_t i = 0; i < result.bands.size(); ++i) {
    result.bands[i] = defaultParametricEqBand(i);
  }
  return result;
}

ParametricEqParams parametricEqParamsFromJson(const nlohmann::json& params)
{
  auto result = defaultParametricEqParams();
  const auto suppliedBands = params.find("bands");
  if (suppliedBands == params.end() || !suppliedBands->is_array()) {
    return result;
  }

  const auto count = std::min<std::size_t>(suppliedBands->size(), kParametricEqBandCount);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& supplied = (*suppliedBands)[i];
    if (!supplied.is_object()) {
      continue;
    }

    auto& band = result.bands[i];
    const auto enabled = supplied.find("enabled");
    if (enabled != supplied.end() && enabled->is_boolean()) {
      band.enabled = enabled->get<bool>();
    }
    band.frequencyHz = finiteClamped(supplied, "frequency_hz", band.frequencyHz,
                                     kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
    band.q = finiteClamped(supplied, "q", band.q, kEqMinimumQ, kEqMaximumQ);
    band.gainDb = finiteClamped(supplied, "gain_db", band.gainDb,
                                kEqMinimumGainDb, kEqMaximumGainDb);
  }

  return result;
}

nlohmann::json parametricEqParamsToJson(const ParametricEqParams& params)
{
  nlohmann::json bands = nlohmann::json::array();
  for (const auto& supplied : params.bands) {
    const auto band = normalized(supplied);
    bands.push_back({
      {"enabled", band.enabled},
      {"frequency_hz", band.frequencyHz},
      {"q", band.q},
      {"gain_db", band.gainDb},
    });
  }
  return {{"mode", "parametric_eq_5"}, {"bands", std::move(bands)}};
}

bool isParametricEqMode(const nlohmann::json& params)
{
  return params.is_object() && params.value("mode", std::string{}) == "parametric_eq_5";
}

} // namespace ardor
