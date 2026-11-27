// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ardor {
// A dual export's complete embedded model owns its controls and calibration.
inline const nlohmann::json& playableNamDocument(const nlohmann::json& document) {
  if (document.contains("embedded_parametric")) {
    const auto& embedded = document.at("embedded_parametric");
    if (!embedded.is_object() || embedded.contains("embedded_parametric"))
      throw std::runtime_error("Invalid embedded parametric NAM");
    return embedded;
  }
  return document;
}

inline nlohmann::json validateNamParameterSpecs(nlohmann::json specs, bool film) {
  const size_t maxParameters = film ? 16 : 24;
  if (!specs.is_array() || specs.empty() || specs.size() > maxParameters)
    throw std::runtime_error("NAM requires 1 to " + std::to_string(maxParameters) + " parameters");
  std::set<std::string> names;
  for (auto& spec : specs) {
    const auto name = spec.at("name").get<std::string>();
    const float low = spec.at("min").get<float>(), high = spec.at("max").get<float>();
    const float value = spec.at("default").get<float>();
    if (name.empty() || !names.insert(name).second || !std::isfinite(low) || !std::isfinite(high)
        || !std::isfinite(value) || low >= high || value < low || value > high)
      throw std::runtime_error("Invalid NAM parameter range, default or name");
    const auto kind = spec.value("type", std::string{"continuous"});
    spec["type"] = kind;
    if (film && (kind != "continuous" || spec.value("steps", 0) > 1))
      throw std::runtime_error("FiLM NAM currently supports continuous parameters");
    if (kind == "switch") {
      const auto labels = spec.at("enum_names").get<std::vector<std::string>>();
      const std::set<std::string> choices(labels.begin(), labels.end());
      if (labels.size() < 2 || choices.size() != labels.size() || choices.contains("")
          || low != 0 || high != labels.size() - 1 || std::trunc(value) != value)
        throw std::runtime_error("Invalid NAM switch choices");
    } else if (kind != "continuous" || (spec.contains("enum_names") && !spec.at("enum_names").empty()))
      throw std::runtime_error("Unsupported NAM parameter type");
  }
  return specs;
}

struct NamVolumeCurve {
  struct Segment {
    double left, right;
    std::array<double, 4> coefficients;
  };
  nlohmann::json parameter;
  size_t index;
  float smoothingSeconds;
  std::vector<Segment> segments;
};

inline std::optional<NamVolumeCurve> namVolumeCurve(const nlohmann::json& model, size_t parameterCount) {
  const auto extensions = model.find("extensions");
  if (extensions == model.end() || !extensions->contains("linear_volume")) return std::nullopt;
  const auto& volume = extensions->at("linear_volume");
  const double low = volume.at("min").get<double>(), high = volume.at("max").get<double>();
  const double defaultValue = volume.at("default").get<double>();
  const double reference = volume.at("reference").get<double>();
  const double smoothingMs = volume.at("smoothing_ms").get<double>();
  const float smoothingSeconds = static_cast<float>(smoothingMs / 1000);
  const auto& index = volume.at("index");
  const auto& segments = volume.at("segments");
  if (volume.at("schema_version") != 1 || volume.at("interpolation") != "pchip"
      || !index.is_number_integer() || index.get<double>() < 0 || index.get<double>() > parameterCount
      || !std::isfinite(defaultValue) || defaultValue < low || defaultValue > high
      || !std::isfinite(reference) || reference < low || reference > high
      || !std::isfinite(smoothingMs) || smoothingMs < 0
      || std::round(48000.0 * smoothingSeconds) >= std::numeric_limits<int>::max()
      || !segments.is_array() || segments.empty())
    throw std::runtime_error("Invalid NAM linear volume extension");

  NamVolumeCurve curve{{{"name",volume.at("name")},{"min",low},{"max",high},
                        {"default",defaultValue},{"type","continuous"}},
                       index.get<size_t>(), smoothingSeconds, {}};
  validateNamParameterSpecs(nlohmann::json::array({curve.parameter}), false);
  double end = low;
  for (const auto& entry : segments) {
    const double left = entry.at("left").get<double>(), right = entry.at("right").get<double>();
    const auto& coefficients = entry.at("coefficients");
    if (left != end || !std::isfinite(right) || right <= left || right > high
        || !coefficients.is_array() || coefficients.size() != 4)
      throw std::runtime_error("Invalid NAM volume segment");
    const auto c = coefficients.get<std::array<double, 4>>();
    const double width = right - left;
    const double bound = ((std::abs(c[0]) * width + std::abs(c[1])) * width + std::abs(c[2])) * width + std::abs(c[3]);
    if (!std::isfinite(bound) || bound > std::numeric_limits<float>::max())
      throw std::runtime_error("Invalid NAM volume coefficients");
    curve.segments.push_back({left,right,c});
    end = right;
  }
  if (end != high) throw std::runtime_error("NAM volume segments must cover the control range");
  return curve;
}

inline nlohmann::json flatNamTierDocument(const nlohmann::json& parent, const nlohmann::json& tier) {
  return {{"architecture",tier.at("architecture")},{"version",parent.at("version")},
          {"sample_rate",parent.at("sample_rate")},{"config",tier.at("config")}};
}

// Compare the same playback fields that Manager reads. Exporters may also
// include capture-planning fields such as step and avoid_zero.
inline nlohmann::json namPlaybackParameterSpecs(const nlohmann::json& specs) {
  auto playback = nlohmann::json::array();
  for (const auto& spec : specs) {
    nlohmann::json control{{"name",spec.at("name")},{"min",spec.at("min")},
                           {"max",spec.at("max")},{"default",spec.at("default")},
                           {"type",spec.at("type")}};
    if (control.at("type") == "switch") control["enum_names"] = spec.at("enum_names");
    playback.push_back(std::move(control));
  }
  return playback;
}

// Shared validation for the device drawer and DSP loader. Empty means snapshot.
// This checks the supported conditioning topology before any DSP allocation.
inline nlohmann::json namParameterSpecs(const nlohmann::json& document) {
  const auto& model = playableNamDocument(document);
  const auto architecture = model.at("architecture").get<std::string>();
  const auto& config = model.at("config");
  if (architecture == "ParametricSlimmableContainer") {
    const auto& tiers = config.at("submodels");
    const auto& weights = model.at("weights");
    if (model.at("version") != "1.0.0" || model.at("sample_rate") != 48000
        || !tiers.is_array() || tiers.empty() || tiers.size() > 8 || !weights.is_array())
      throw std::runtime_error("Invalid flat parametric NAM container");
    auto specs = validateNamParameterSpecs(config.at("params"), false);
    const auto playbackSpecs = namPlaybackParameterSpecs(specs);
    double previousBoundary = 0;
    size_t nextWeight = 0;
    for (const auto& tier : tiers) {
      const double boundary = tier.at("max_value").get<double>();
      const auto& offset = tier.at("weight_offset");
      const auto& count = tier.at("weight_count");
      if (tier.at("architecture") != "HyperWaveNet" || !std::isfinite(boundary)
          || boundary <= previousBoundary || boundary > 1
          || !offset.is_number_integer() || offset.get<double>() != nextWeight
          || !count.is_number_integer() || count.get<double>() <= 0
          || count.get<double>() > weights.size() - nextWeight)
        throw std::runtime_error("Invalid flat HyperWaveNet NAM tier");
      previousBoundary = boundary;
      nextWeight += count.get<size_t>();
      if (namPlaybackParameterSpecs(namParameterSpecs(flatNamTierDocument(model, tier))) != playbackSpecs)
        throw std::runtime_error("NAM tiers must share the container's parameter definitions");
    }
    if (previousBoundary != 1 || nextWeight != weights.size())
      throw std::runtime_error("NAM tiers must cover 1.0 and the complete weight payload");
    if (const auto volume = namVolumeCurve(model, specs.size())) {
      specs.insert(specs.begin() + static_cast<std::ptrdiff_t>(volume->index), volume->parameter);
      return validateNamParameterSpecs(std::move(specs), false);
    }
    return specs;
  }
  if (architecture == "SlimmableContainer") {
    const auto& tiers = config.at("submodels");
    if (!tiers.is_array() || tiers.empty() || tiers.size() > 8)
      throw std::runtime_error("NAM container requires 1 to 8 tiers");
    bool parametric = false;
    for (const auto& tier : tiers) {
      const auto kind = tier.at("model").value("architecture", std::string{});
      parametric |= kind == "ParametricWaveNet" || kind == "ConcatWaveNet" || kind == "ConcatLSTM"
        || kind == "HyperWaveNet" || tier.at("model").at("config").contains("params")
        || tier.at("model").at("config").contains("parametric");
    }
    if (!parametric) return nlohmann::json::array(); // stock container rules stay upstream
    const auto family = tiers.at(0).at("model").at("architecture").get<std::string>();
    const bool concat = family == "ConcatWaveNet";
    if ((!concat && family != "ParametricWaveNet") || model.at("version") != (concat ? "1.0.0" : "0.7.0")
        || model.at("sample_rate") != 48000)
      throw std::runtime_error("Unsupported parametric NAM container family, version or sample rate");
    nlohmann::json specs;
    double previous = 0;
    for (const auto& tier : tiers) {
      const double boundary = tier.at("max_value").get<double>();
      if (!std::isfinite(boundary) || boundary <= previous || boundary > 1)
        throw std::runtime_error("Invalid NAM tier boundaries");
      previous = boundary;
      const auto& child = tier.at("model");
      if (child.at("architecture") != family || child.contains("embedded_parametric")
          || child.at("version") != model.at("version") || child.at("sample_rate") != model.at("sample_rate"))
        throw std::runtime_error("NAM tiers must share architecture, version and sample rate");
      if (concat) {
        const auto metadata = model.value("metadata", nlohmann::json::object());
        const auto childMetadata = child.value("metadata", nlohmann::json::object());
        if ((metadata.value("gear_type", std::string{"amp"}) == "pedal") != (childMetadata.value("gear_type", std::string{"amp"}) == "pedal")
            || metadata.value("input_level_dbu", nlohmann::json{}) != childMetadata.value("input_level_dbu", nlohmann::json{}))
          throw std::runtime_error("NAM tiers must share gear type and input calibration");
      }
      const auto definitions = namParameterSpecs(child);
      if (specs.is_null()) specs = definitions;
      else if (specs != definitions) throw std::runtime_error("NAM tiers must share parameter definitions");
    }
    if (previous != 1) throw std::runtime_error("NAM final tier must cover 1.0");
    return specs;
  }
  const bool film = architecture == "ParametricWaveNet";
  const bool concat = architecture == "ConcatWaveNet" || architecture == "ConcatLSTM";
  const bool hyper = architecture == "HyperWaveNet";
  if (!film && !concat && !hyper) {
    if (architecture == "HyperWaveNet" || config.contains("params") || config.contains("parametric"))
      throw std::runtime_error("Unsupported parametric NAM architecture");
    return nlohmann::json::array();
  }
  if (model.at("version") != (film ? "0.7.0" : "1.0.0") || model.at("sample_rate") != 48000)
    throw std::runtime_error("Unsupported parametric NAM version or sample rate");
  if (hyper) {
    if (!config.contains("hypernet") || !config.at("hypernet").is_object()
        || (config.contains("condition_dsp") && !config.at("condition_dsp").is_null()))
      throw std::runtime_error("HyperWaveNet requires hypernet and no nested condition DSP");
    const auto& layers = config.at("layers");
    if (!layers.is_array() || layers.empty() || layers.size() > 8
        || layers.at(0).value("input_size", 1) != 1)
      throw std::runtime_error("HyperWaveNet requires 1 to 8 mono layer arrays");
    for (const auto& layer : layers)
      if (layer.value("condition_size", 1) != 1
          || (layer.contains("packing") && !layer.at("packing").is_null())
          || (layer.contains("slimmable") && !layer.at("slimmable").is_null()))
        throw std::runtime_error("HyperWaveNet does not support packed, slimmable or multichannel conditioning");
  }
  auto specs = film ? config.at("parametric").at("parameters") : config.at("params");
  specs = validateNamParameterSpecs(std::move(specs), film);
  if (film) {
    const auto& conditioning = config.at("parametric");
    if (conditioning.at("schema_version") != 1 || conditioning.at("head_mode") != "skip"
        || conditioning.value("type", std::string{"film"}) != "film" || conditioning.contains("lora")
        || !conditioning.at("condition_size").is_number_integer()
        || conditioning.at("condition_size").get<size_t>() != specs.size()
        || !config.at("layers").is_number_integer())
      throw std::runtime_error("Unsupported FiLM NAM schema; requires schema 1 and skip head");
    const int channels = config.at("layers").get<int>();
    if (channels < 3 || channels > 9 || !std::isfinite(config.at("head_scale").get<float>()))
      throw std::runtime_error("FiLM NAM supports A2 widths 3 to 9");
    if (conditioning.contains("film_layers")) {
      const auto& layers = conditioning.at("film_layers");
      if (!layers.is_array() || layers.size() != 23) throw std::runtime_error("FiLM requires all 23 A2 layers");
      for (size_t i = 0; i < 23; ++i)
        if (layers[i] != "layer_" + std::to_string(i)) throw std::runtime_error("Invalid FiLM layer order");
    }
  }
  return specs;
}
} // namespace ardor
