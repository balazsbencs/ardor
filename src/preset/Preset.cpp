#include "preset/Preset.h"

#include <stdexcept>

namespace ardor {

namespace {

void requireSerialRouting(const std::string& routing)
{
  if (routing != "serial") {
    throw std::invalid_argument("preset routing must be serial");
  }
}

} // namespace

nlohmann::json toJson(const Preset& preset)
{
  requireSerialRouting(preset.routing);

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
    preset.blocks.push_back(block);
  }

  return preset;
}

} // namespace ardor
