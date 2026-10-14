// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <cmath>
#include <stdexcept>
#include <set>
#include <string>

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

// Shared validation for the device drawer and DSP loader. Empty means snapshot.
// This checks the supported conditioning topology before any DSP allocation.
inline nlohmann::json namParameterSpecs(const nlohmann::json& document) {
  const auto& model = playableNamDocument(document);
  const auto architecture = model.at("architecture").get<std::string>();
  const auto& config = model.at("config");
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
  if (!film && !concat) {
    if (architecture == "HyperWaveNet" || config.contains("params") || config.contains("parametric"))
      throw std::runtime_error("Unsupported parametric NAM architecture");
    return nlohmann::json::array();
  }
  if (model.at("version") != (film ? "0.7.0" : "1.0.0") || model.at("sample_rate") != 48000)
    throw std::runtime_error("Unsupported parametric NAM version or sample rate");
  auto specs = film ? config.at("parametric").at("parameters") : config.at("params");
  const size_t maxParameters = film ? 16 : 24;
  if (!specs.is_array() || specs.empty() || specs.size() > maxParameters)
    throw std::runtime_error("NAM requires 1 to " + std::to_string(maxParameters) + " parameters");
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
} // namespace ardor
