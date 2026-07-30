#include "preset/Preset.h"

#include <filesystem>
#include <stdexcept>

namespace ardor {

namespace {

void requireSerialRouting(const std::string& routing)
{
  if (routing != "serial") {
    throw std::invalid_argument("preset routing must be serial");
  }
}

void normalizeLegacyEffectBlock(PresetBlock& block);

void validateBlockAssets(const std::vector<PresetBlock>& blocks, int version, bool insideLane = false)
{
  for (const auto& block : blocks) {
    if (!block.asset.empty() && !isValidBlockAssetPath(block.asset)) {
      throw std::invalid_argument("preset asset must stay under data root");
    }
    if (block.type == "dualRig") {
      if (version != 2) {
        throw std::invalid_argument("dual rig requires preset version 2");
      }
      if (insideLane) {
        throw std::invalid_argument("nested dual rig blocks are not supported");
      }
      if (block.lanes[0].empty() || block.lanes[1].empty()) {
        throw std::invalid_argument("dual rig requires non-empty left and right lanes");
      }
      validateBlockAssets(block.lanes[0], version, true);
      validateBlockAssets(block.lanes[1], version, true);
      continue;
    }
    if (block.type != "dualAmp") {
      continue;
    }
    if (insideLane) {
      throw std::invalid_argument("dual rig lanes cannot contain split blocks");
    }
    if (!block.params.is_object()) continue;
    for (const char* key : {"leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"}) {
      const auto it = block.params.find(key);
      if (it == block.params.end() || !it->is_string()
          || (!it->get_ref<const std::string&>().empty()
              && !isValidBlockAssetPath(it->get_ref<const std::string&>()))) {
        throw std::invalid_argument(std::string{"dual amp asset must stay under data root: "} + key);
      }
    }
  }
}

nlohmann::json blockToJson(const PresetBlock& block)
{
  nlohmann::json json = {
    {"id", block.id},
    {"type", block.type},
    {"enabled", block.enabled},
    {"asset", block.asset},
    {"params", block.params.is_null() ? nlohmann::json::object() : block.params},
  };
  if (block.type == "dualRig") {
    nlohmann::json left = nlohmann::json::array();
    nlohmann::json right = nlohmann::json::array();
    for (const auto& child : block.lanes[0]) left.push_back(blockToJson(child));
    for (const auto& child : block.lanes[1]) right.push_back(blockToJson(child));
    json["lanes"] = {
      {"left", {{"blocks", std::move(left)}}},
      {"right", {{"blocks", std::move(right)}}},
    };
  }
  return json;
}

PresetBlock blockFromJson(const nlohmann::json& json, bool insideLane)
{
  PresetBlock block;
  block.id = json.at("id").get<std::string>();
  block.type = json.at("type").get<std::string>();
  block.enabled = json.value("enabled", true);
  block.asset = json.value("asset", "");
  block.params = json.value("params", nlohmann::json::object());
  normalizeLegacyEffectBlock(block);
  if (block.type == "dualRig") {
    if (insideLane) {
      throw std::invalid_argument("nested dual rig blocks are not supported");
    }
    const auto& lanes = json.at("lanes");
    for (const auto& child : lanes.at("left").at("blocks")) {
      block.lanes[0].push_back(blockFromJson(child, true));
    }
    for (const auto& child : lanes.at("right").at("blocks")) {
      block.lanes[1].push_back(blockFromJson(child, true));
    }
  }
  return block;
}

void normalizeLegacyEffectBlock(PresetBlock& block)
{
  if (!block.params.is_object()) {
    return;
  }

  const auto mode = block.params.find("mode");
  const bool hasMode = mode != block.params.end() && mode->is_string() && !mode->get_ref<const std::string&>().empty();

  // Early UI builds wrote these generic placeholder types. They never had a
  // runtime implementation or parameter descriptors; map them to the effects
  // that those placeholders represented.
  if (block.type == "time") {
    block.type = "delay";
    if (!hasMode) block.params["mode"] = "tape";
  } else if (block.type == "modulation") {
    block.type = "mod";
    if (!hasMode) block.params["mode"] = "chorus";
  } else if (block.type == "dynamics" && !hasMode) {
    block.params["mode"] = "compressor";
  }
}

} // namespace

bool isValidBlockAssetPath(std::string_view asset)
{
  if (asset.empty()) {
    return true;
  }

  const std::filesystem::path path(asset);
  if (path.is_absolute()) {
    return false;
  }

  for (const auto& part : path) {
    if (part == "..") {
      return false;
    }
  }

  return true;
}

nlohmann::json toJson(const Preset& preset)
{
  if (preset.version != 1 && preset.version != 2) {
    throw std::invalid_argument("preset version must be 1 or 2");
  }
  requireSerialRouting(preset.routing);
  validateBlockAssets(preset.blocks, preset.version);

  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& block : preset.blocks) {
    blocks.push_back(blockToJson(block));
  }

  return {
    {"version", preset.version},
    {"name", preset.name},
    {"routing", preset.routing},
    {"global", {
      {"inputGainDb", preset.global.inputGainDb},
      {"outputGainDb", preset.global.outputGainDb},
      {"safetyLimitDb", preset.global.safetyLimitDb},
    }},
    {"blocks", blocks},
  };
}

Preset presetFromJson(const nlohmann::json& json)
{
  Preset preset;
  preset.version = json.at("version").get<int>();
  if (preset.version != 1 && preset.version != 2) {
    throw std::invalid_argument("preset version must be 1 or 2");
  }
  preset.name = json.value("name", "");
  preset.routing = json.at("routing").get<std::string>();
  requireSerialRouting(preset.routing);

  const auto& global = json.at("global");
  preset.global.inputGainDb = global.value("inputGainDb", 0.0f);
  preset.global.outputGainDb = global.value("outputGainDb", 0.0f);
  preset.global.safetyLimitDb = global.value("safetyLimitDb", -1.0f);

  for (const auto& blockJson : json.at("blocks")) {
    preset.blocks.push_back(blockFromJson(blockJson, false));
  }

  validateBlockAssets(preset.blocks, preset.version);

  return preset;
}

} // namespace ardor
