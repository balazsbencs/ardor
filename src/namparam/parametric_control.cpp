// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#include "parametric_control.h"

#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{

bool is_int_like(const float value)
{
  return std::isfinite(value) && std::trunc(value) == value;
}

} // namespace

namespace nam
{

// Shared parser for the `config["params"]` array used by HyperWaveNet and ConcatWaveNet.
// Mirrors ParamSpec.__post_init__ in nam/models/parametric/_spec.py. Where the two
// pre-existing per-architecture parsers had drifted, this takes the stricter behaviour
// of the two (e.g. enum_names non-empty + uniqueness, previously only enforced for
// ConcatWaveNet).
std::vector<ParamSpec> parse_param_specs(const nlohmann::json& config, const std::string& model_name)
{
  if (!config.contains("params"))
    throw std::runtime_error(model_name + " config missing 'params' array.");

  const auto& params_json = config.at("params");
  if (!params_json.is_array())
    throw std::runtime_error(model_name + " config: 'params' must be an array of objects.");
  if (params_json.empty())
    throw std::runtime_error(model_name + " config: 'params' array must contain at least one parameter.");

  std::vector<ParamSpec> params;
  std::unordered_set<std::string> seen_names;
  params.reserve(params_json.size());
  for (size_t i = 0; i < params_json.size(); ++i)
  {
    const auto& entry = params_json.at(i);
    const auto where = model_name + " config: params[" + std::to_string(i) + "]";
    if (!entry.is_object())
      throw std::runtime_error(where + " must be an object.");
    if (!entry.contains("name") || !entry.contains("min") || !entry.contains("max") || !entry.contains("default"))
      throw std::runtime_error(where + " must define name/min/max/default.");

    ParamSpec spec;
    spec.name = entry.at("name").get<std::string>();
    spec.min = entry.at("min").get<float>();
    spec.max = entry.at("max").get<float>();
    spec.defaultValue = entry.at("default").get<float>();
    spec.type = entry.value("type", std::string("continuous"));
    if (entry.contains("enum_names") && !entry.at("enum_names").is_null())
      spec.enum_names = entry.at("enum_names").get<std::vector<std::string>>();
    // Note: the trainer also writes "step"/"avoid_zero" keys into every params[] entry
    // (capture-planning metadata; see nam/models/parametric/_spec.py). The runtime has no
    // use for them and deliberately ignores them here rather than parsing or validating them.

    const auto named = model_name + " config: param '" + spec.name + "'";
    if (spec.name.empty())
      throw std::runtime_error(where + " name must be non-empty.");
    if (!seen_names.insert(spec.name).second)
      throw std::runtime_error(named + " duplicates an earlier parameter name.");
    if (!std::isfinite(spec.min) || !std::isfinite(spec.max) || !std::isfinite(spec.defaultValue))
      throw std::runtime_error(named + " has non-finite min/max/default.");
    if (spec.min >= spec.max)
      throw std::runtime_error(named + " must satisfy min < max.");
    if (spec.defaultValue < spec.min || spec.defaultValue > spec.max)
      throw std::runtime_error(named + " default must lie within [min, max].");

    if (spec.type == "continuous")
    {
      if (!spec.enum_names.empty())
        throw std::runtime_error(named + " is continuous and cannot define enum_names.");
      params.push_back(std::move(spec));
      continue;
    }

    if (spec.type != "switch")
      throw std::runtime_error(named + " has unsupported type '" + spec.type + "'.");
    if (spec.enum_names.size() < 2)
      throw std::runtime_error(named + " switch parameters require at least two enum_names.");
    std::unordered_set<std::string> seen_enum_names;
    for (const auto& enum_name : spec.enum_names)
    {
      if (enum_name.empty() || !seen_enum_names.insert(enum_name).second)
        throw std::runtime_error(named + " enum_names must be non-empty and unique.");
    }
    if (!is_int_like(spec.min) || !is_int_like(spec.max) || !is_int_like(spec.defaultValue))
      throw std::runtime_error(named + " switch min/max/default must be integer indices.");

    const auto expected_max = static_cast<int>(spec.enum_names.size()) - 1;
    const auto min_index = static_cast<int>(spec.min);
    const auto max_index = static_cast<int>(spec.max);
    const auto default_index = static_cast<int>(spec.defaultValue);
    if (min_index != 0 || max_index != expected_max)
      throw std::runtime_error(named + " switch range must be [0, " + std::to_string(expected_max) + "].");
    if (default_index < min_index || default_index > max_index)
      throw std::runtime_error(named + " default switch index must lie within range.");

    params.push_back(std::move(spec));
  }

  return params;
}

} // namespace nam
