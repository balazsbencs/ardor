// SPDX-License-Identifier: MIT
#pragma once

#include "json.hpp"
#include <array>
#include <filesystem>
#include <fstream>

struct FlatHyperWaveNetFixture {
  using Json = nlohmann::json;
  std::array<Json, 2> tiers;
  Json document;

  explicit FlatHyperWaveNetFixture(const std::filesystem::path& source) {
    tiers[0] = Json::parse(std::ifstream(source / "tests/fixtures/hyperwavenet/hyperwavenet.nam"));
    tiers[0]["metadata"] = {{"gear_type", "pedal"}};
    tiers[1] = tiers[0];
    tiers[1]["weights"][0] = -tiers[0]["weights"][0].get<float>();

    document = {{"architecture", "ParametricSlimmableContainer"}, {"version", "1.0.0"},
                {"sample_rate", 48000}, {"metadata", tiers[0]["metadata"]}, {"weights", Json::array()}};
    auto& config = document["config"];
    config["params"] = tiers[0]["config"]["params"];
    config["params"][0]["step"] = 0.1;
    config["params"][0]["enum_names"] = Json::array();
    auto& weights = document["weights"];
    for (std::size_t index = 0; index < tiers.size(); ++index) {
      const auto& tier = tiers[index];
      const auto& payload = tier.at("weights");
      config["submodels"].push_back({{"architecture", "HyperWaveNet"}, {"config", tier.at("config")},
        {"max_value", (index + 1) * 0.5}, {"weight_offset", weights.size()}, {"weight_count", payload.size()}});
      for (const auto& weight : payload) weights.push_back(weight);
    }
    document["extensions"]["linear_volume"] = {
      {"schema_version", 1}, {"interpolation", "pchip"}, {"name", "Volume"}, {"index", 1},
      {"min", 0}, {"max", 10}, {"default", 5}, {"reference", 10}, {"smoothing_ms", 5},
      {"segments", Json::array({
        {{"left", 0}, {"right", 5}, {"coefficients", {0.002, 0, 0, 0}}},
        {{"left", 5}, {"right", 10}, {"coefficients", {0.001, 0, 0.15, 0.25}}}
      })}
    };
  }
};
