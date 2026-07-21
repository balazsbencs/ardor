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

void validateBlockAssets(const std::vector<PresetBlock>& blocks)
{
  for (const auto& block : blocks) {
    if (!block.asset.empty() && !isValidBlockAssetPath(block.asset)) {
      throw std::invalid_argument("preset asset must stay under data root");
    }
  }
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
  requireSerialRouting(preset.routing);
  validateBlockAssets(preset.blocks);

  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& block : preset.blocks) {
    blocks.push_back({
      {"id", block.id},
      {"type", block.type},
      {"enabled", block.enabled},
      {"asset", block.asset},
      {"params", block.params.is_null() ? nlohmann::json::object() : block.params},
    });
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
  preset.name = json.value("name", "");
  preset.routing = json.at("routing").get<std::string>();
  requireSerialRouting(preset.routing);

  const auto& global = json.at("global");
  preset.global.inputGainDb = global.value("inputGainDb", 0.0f);
  preset.global.outputGainDb = global.value("outputGainDb", 0.0f);
  preset.global.safetyLimitDb = global.value("safetyLimitDb", -1.0f);

  for (const auto& blockJson : json.at("blocks")) {
    PresetBlock block;
    block.id = blockJson.at("id").get<std::string>();
    block.type = blockJson.at("type").get<std::string>();
    block.enabled = blockJson.value("enabled", true);
    block.asset = blockJson.value("asset", "");
    block.params = blockJson.value("params", nlohmann::json::object());
    normalizeLegacyEffectBlock(block);
    preset.blocks.push_back(block);
  }

  validateBlockAssets(preset.blocks);

  return preset;
}

} // namespace ardor
