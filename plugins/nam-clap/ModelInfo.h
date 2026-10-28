// SPDX-License-Identifier: MIT
#pragma once
#include "dsp/NamProcessor.h"
#include "preset/NamMetadata.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>

namespace ardor::nam_clap {
using Json = nlohmann::json;
constexpr std::size_t modelLimit = 64 * 1024 * 1024;
enum Parameter : uint32_t { Input = 1, Output, Bypass, Channel, Calibrate, Reference, Size };
struct Control {
  uint32_t id;
  std::string name;
  double min, max, initial;
  std::vector<std::string> choices;
  bool stepped() const { return !choices.empty(); }
};
inline const std::array<Control,6> globalControls{{
  {Input,"Input trim",-24,24,0,{}},{Output,"Output trim",-60,12,0,{}},
  {Bypass,"Bypass",0,1,0,{"Active","Bypassed"}},
  {Channel,"Guitar input",0,2,0,{"Left","Right","Average"}},
  {Calibrate,"Calibrate input",0,1,0,{"Off","On"}},
  {Reference,"Interface reference",-30,40,0,{}}
}};
struct Tier { std::string name; float size; };
struct ModelInfo {
  std::shared_ptr<const Json> document;
  std::string name, architecture, gear;
  std::vector<Control> controls;
  std::vector<Tier> tiers;
  std::optional<double> inputLevel;

  static ModelInfo parse(Json json, std::string name) {
    ModelInfo result;
    result.document = std::make_shared<const Json>(std::move(json));
    const auto& playable = playableNamDocument(*result.document);
    const auto specs = namParameterSpecs(playable);
    result.name = std::move(name);
    result.architecture = playable.at("architecture").get<std::string>();
    auto metadata = playable.value("metadata", Json::object());
    if(!metadata.is_object())metadata=Json::object();
    result.gear = metadata.contains("gear_type")&&metadata["gear_type"].is_string()
      ? metadata["gear_type"].get<std::string>() : "amp";
    if (metadata.contains("input_level_dbu") && metadata["input_level_dbu"].is_number()) {
      const double value = metadata["input_level_dbu"].get<double>();
      if (!std::isfinite(value)) throw std::runtime_error("Invalid model input reference");
      result.inputLevel = value;
    }
    std::set<uint32_t> ids;
    for (const auto& spec : specs) {
      const auto choices = spec.contains("enum_names") && !spec["enum_names"].is_null()
        ? spec["enum_names"].get<std::vector<std::string>>() : std::vector<std::string>{};
      // IDs describe physical controls, not row positions. Reordered compatible
      // controls keep their automation; changed ranges/types get a new identity.
      const auto identity = Json::array({spec.at("name"),spec.at("min").get<double>(),spec.at("max").get<double>(),
          spec.at("type"),choices}).dump();
      uint32_t hash = 2166136261u;
      for (unsigned char c : identity) hash = (hash ^ c) * 16777619u;
      hash = 0x10000000u | (hash & 0x0fffffffu);
      if (!ids.insert(hash).second) throw std::runtime_error("Model control IDs collide");
      result.controls.push_back({hash,spec.at("name"),spec.at("min").get<double>(),
        spec.at("max").get<double>(),spec.at("default").get<double>(),
        choices});
    }
    if (result.architecture == "SlimmableContainer") {
      const auto& models = playable.at("config").at("submodels");
      if (!models.is_array() || models.empty() || models.size() > 8)
        throw std::runtime_error("Model container requires 1 to 8 tiers");
      double previous = 0;
      for (std::size_t i = 0; i < models.size(); ++i) {
        const double high = models[i].at("max_value").get<double>();
        if (!std::isfinite(high) || high <= previous || high > 1)
          throw std::runtime_error("Invalid model size boundaries");
        const std::string label = models.size() == 1 ? "Single model" : i == 0 ? "Nano" :
          i + 1 == models.size() ? "Standard" : "Tier " + std::to_string(i + 1);
        result.tiers.push_back({label,static_cast<float>((previous + high) * .5)});
        previous = high;
      }
      if (previous != 1) throw std::runtime_error("Model sizes must cover the full range");
    } else {
      // Upstream WaveNets can also expose channel-sliced sizes without a
      // container. Ask the runtime for their actual boundaries.
      if(result.architecture=="WaveNet") {
        const auto& config=playable.at("config");
        const auto layers=config.value("layers",Json::array());
        const bool sliced=layers.is_array()&&std::any_of(layers.begin(),layers.end(),[](const auto& layer){return layer.contains("slimmable");});
        if(sliced) {
          auto probe=nam::get_dsp(playable);
          if(const auto* slim=dynamic_cast<const nam::SlimmableModel*>(probe.get())) {
            auto boundaries=slim->GetSlimmableSizeBreakpoints();boundaries.push_back(1);
            double low=0;
            for(std::size_t i=0;i<boundaries.size();++i) {
              const double high=boundaries[i];
              if(!std::isfinite(high)||high<=low||high>1)throw std::runtime_error("Invalid native model size boundaries");
              result.tiers.push_back({boundaries.size()==1?"Single model":i+1==boundaries.size()?"Full":"Size "+std::to_string(i+1),static_cast<float>((low+high)*.5)});
              low=high;
            }
          }
        }
      }
      if(result.tiers.empty())result.tiers.push_back({"Single model",1});
    }
    return result;
  }
  static ModelInfo read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > modelLimit) throw std::runtime_error("Model exceeds the 64 MiB limit");
    std::ifstream stream(path);
    return parse(Json::parse(stream),path.filename().string());
  }
};
} // namespace ardor::nam_clap
