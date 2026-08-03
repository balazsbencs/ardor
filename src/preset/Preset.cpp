#include "preset/Preset.h"

#include <algorithm>
#include <cmath>
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

bool containsBlockId(const std::vector<PresetBlock>& blocks, std::string_view id)
{
  for (const auto& block : blocks) {
    if (block.id == id) return true;
    if (containsBlockId(block.lanes[0], id) || containsBlockId(block.lanes[1], id)) {
      return true;
    }
  }
  return false;
}

void validateExpression(const Preset& preset)
{
  if (!preset.expression) return;
  const auto& expression = *preset.expression;
  if (expression.blockId.empty() || expression.parameter.empty()) {
    throw std::invalid_argument("expression assignment requires blockId and parameter");
  }
  if (!containsBlockId(preset.blocks, expression.blockId)) {
    throw std::invalid_argument("expression assignment block does not exist");
  }
  if (!std::isfinite(expression.minimum) || !std::isfinite(expression.maximum)
      || expression.minimum > expression.maximum) {
    throw std::invalid_argument("expression assignment range is invalid");
  }
}

bool isTopLevelBlockId(const Preset& preset, std::string_view id)
{
  return std::any_of(preset.blocks.begin(), preset.blocks.end(),
                     [id](const PresetBlock& block) { return block.id == id; });
}

void validateMidiBindings(const Preset& preset)
{
  for (std::size_t index = 0; index < preset.midiBindings.size(); ++index) {
    const auto& binding = preset.midiBindings[index];
    if (binding.channel < -1 || binding.channel > 15 || binding.controlChange > 127) {
      throw std::invalid_argument("MIDI binding channel or controller is invalid");
    }
    if (binding.actions.empty()) {
      throw std::invalid_argument("MIDI binding requires at least one action");
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& other = preset.midiBindings[previous];
      if (binding.controlChange == other.controlChange
          && (binding.channel == -1 || other.channel == -1
              || binding.channel == other.channel)) {
        throw std::invalid_argument("MIDI bindings must not overlap on a controller");
      }
    }
    for (const auto& action : binding.actions) {
      if (action.blockId.empty() || !isTopLevelBlockId(preset, action.blockId)) {
        throw std::invalid_argument("MIDI action target block does not exist at top level");
      }
      if (action.target == PresetMidiTargetType::Parameter && action.parameter.empty()) {
        throw std::invalid_argument("MIDI parameter action requires a parameter");
      }
      if (!std::isfinite(action.value1) || !std::isfinite(action.value2)) {
        throw std::invalid_argument("MIDI action values must be finite");
      }
    }
  }
}

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
  validateExpression(preset);
  validateMidiBindings(preset);

  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& block : preset.blocks) {
    blocks.push_back(blockToJson(block));
  }

  nlohmann::json json = {
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
  if (preset.expression) {
    json["expression"] = {
      {"blockId", preset.expression->blockId},
      {"parameter", preset.expression->parameter},
      {"minimum", preset.expression->minimum},
      {"maximum", preset.expression->maximum},
      {"inverted", preset.expression->inverted},
    };
  }
  if (!preset.midiBindings.empty()) {
    json["midiMappings"] = nlohmann::json::array();
    for (const auto& binding : preset.midiBindings) {
      nlohmann::json actions = nlohmann::json::array();
      for (const auto& action : binding.actions) {
        nlohmann::json actionJson = {
          {"target", action.target == PresetMidiTargetType::BlockEnabled
            ? "blockEnabled" : "parameter"},
          {"blockId", action.blockId},
          {"value1", action.value1},
          {"value2", action.value2},
        };
        if (action.target == PresetMidiTargetType::Parameter) {
          actionJson["parameter"] = action.parameter;
        }
        actions.push_back(std::move(actionJson));
      }
      json["midiMappings"].push_back({
        {"channel", binding.channel},
        {"controlChange", binding.controlChange},
        {"mode", binding.mode == PresetMidiBindingMode::Toggle ? "toggle" : "continuous"},
        {"actions", std::move(actions)},
      });
    }
  }
  return json;
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
  if (const auto expression = json.find("expression");
      expression != json.end() && !expression->is_null()) {
    preset.expression = PresetExpression{
      expression->at("blockId").get<std::string>(),
      expression->at("parameter").get<std::string>(),
      expression->value("minimum", 0.0f),
      expression->value("maximum", 1.0f),
      expression->value("inverted", false),
    };
  }
  validateExpression(preset);

  if (const auto mappings = json.find("midiMappings");
      mappings != json.end() && !mappings->is_null()) {
    if (!mappings->is_array()) {
      throw std::invalid_argument("MIDI mappings must be an array");
    }
    for (const auto& bindingJson : *mappings) {
      PresetMidiBinding binding;
      binding.channel = bindingJson.value("channel", -1);
      const int controlChange = bindingJson.at("controlChange").get<int>();
      if (controlChange < 0 || controlChange > 127) {
        throw std::invalid_argument("MIDI controller must be between 0 and 127");
      }
      binding.controlChange = static_cast<std::uint8_t>(controlChange);
      const std::string mode = bindingJson.value("mode", "continuous");
      if (mode == "toggle") binding.mode = PresetMidiBindingMode::Toggle;
      else if (mode != "continuous") throw std::invalid_argument("unknown MIDI binding mode");
      for (const auto& actionJson : bindingJson.at("actions")) {
        const std::string target = actionJson.value("target", "parameter");
        PresetMidiAction action;
        if (target == "blockEnabled") action.target = PresetMidiTargetType::BlockEnabled;
        else if (target != "parameter") throw std::invalid_argument("unknown MIDI action target");
        action.blockId = actionJson.at("blockId").get<std::string>();
        action.parameter = actionJson.value("parameter", "");
        action.value1 = actionJson.at("value1").get<float>();
        action.value2 = actionJson.at("value2").get<float>();
        binding.actions.push_back(std::move(action));
      }
      preset.midiBindings.push_back(std::move(binding));
    }
  }
  validateMidiBindings(preset);

  return preset;
}

float expressionValueAt(const PresetExpression& assignment, float normalizedPosition)
{
  float position = std::clamp(normalizedPosition, 0.0f, 1.0f);
  if (assignment.inverted) position = 1.0f - position;
  return assignment.minimum + position * (assignment.maximum - assignment.minimum);
}

float midiActionValueAt(const PresetMidiAction& action, std::uint8_t controlValue)
{
  const float position = static_cast<float>(std::min<int>(controlValue, 127)) / 127.0f;
  return action.value1 + position * (action.value2 - action.value1);
}

} // namespace ardor
