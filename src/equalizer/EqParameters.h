#pragma once

#include <array>
#include <cstddef>

#include <nlohmann/json.hpp>

namespace ardor {

inline constexpr std::size_t kParametricEqBandCount = 5;
inline constexpr float kEqMinimumFrequencyHz = 20.0f;
inline constexpr float kEqMaximumFrequencyHz = 20000.0f;
inline constexpr float kEqMinimumQ = 0.1f;
inline constexpr float kEqMaximumQ = 18.0f;
inline constexpr float kEqMinimumGainDb = -18.0f;
inline constexpr float kEqMaximumGainDb = 18.0f;

enum class EqPassFilterKind {
  HighPass,
  LowPass,
};

struct EqPassFilterParams {
  bool enabled = false;
  float frequencyHz = 1000.0f;
  float q = 0.70710678f;

  bool operator==(const EqPassFilterParams&) const = default;
};

struct EqBandParams {
  bool enabled = true;
  float frequencyHz = 1000.0f;
  float q = 1.0f;
  float gainDb = 0.0f;

  bool operator==(const EqBandParams&) const = default;
};

struct ParametricEqParams {
  EqPassFilterParams highPass{};
  std::array<EqBandParams, kParametricEqBandCount> bands{};
  EqPassFilterParams lowPass{};

  bool operator==(const ParametricEqParams&) const = default;
};

EqBandParams defaultParametricEqBand(std::size_t index);
EqPassFilterParams defaultEqPassFilter(EqPassFilterKind kind);
ParametricEqParams defaultParametricEqParams();
ParametricEqParams parametricEqParamsFromJson(const nlohmann::json& params);
nlohmann::json parametricEqParamsToJson(const ParametricEqParams& params);
bool isParametricEqMode(const nlohmann::json& params);

} // namespace ardor
