#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>
#include <string>
#include <vector>

namespace ardor {

struct PresetGlobal {
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;
  float safetyLimitDb = -1.0f;
};

struct PresetBlock {
  std::string id;
  std::string type;
  bool enabled = true;
  std::string asset;
  nlohmann::json params = nlohmann::json::object();
  // Version-2 Dual Rig blocks own two ordinary serial child chains. Other
  // block types leave both vectors empty. Nested Dual Rig blocks are rejected
  // during validation so the runtime remains a single split/merge region.
  std::array<std::vector<PresetBlock>, 2> lanes;
};

struct Preset {
  int version = 1;
  std::string name;
  std::string routing = "serial";
  PresetGlobal global;
  std::vector<PresetBlock> blocks;
};

nlohmann::json toJson(const Preset& preset);
Preset presetFromJson(const nlohmann::json& json);
bool isValidBlockAssetPath(std::string_view asset);

} // namespace ardor
