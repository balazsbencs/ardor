#include "preset/Preset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace ardor {

namespace {

void requireRouting(const std::string& routing)
{
  if (routing != "serial" && routing != "wdw") {
    throw std::invalid_argument("preset routing must be serial or wdw");
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

bool containsWdwBlockId(const WdwRouting& routing, std::string_view id)
{
  return containsBlockId(routing.dry.blocks, id)
      || containsBlockId(routing.wet.blocks, id);
}

void validateExpression(const Preset& preset)
{
  if (!preset.expression) return;
  const auto& expression = *preset.expression;
  if (expression.blockId.empty() || expression.parameter.empty()) {
    throw std::invalid_argument("expression assignment requires blockId and parameter");
  }
  const bool exists = containsBlockId(preset.blocks, expression.blockId)
    || (preset.wdw && containsWdwBlockId(*preset.wdw, expression.blockId));
  if (!exists) {
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
  std::size_t actionCount = 0;
  for (std::size_t index = 0; index < preset.midiBindings.size(); ++index) {
    const auto& binding = preset.midiBindings[index];
    if (binding.channel < -1 || binding.channel > 15 || binding.controlChange > 127) {
      throw std::invalid_argument("MIDI binding channel or controller is invalid");
    }
    if (binding.actions.empty()) {
      throw std::invalid_argument("MIDI binding requires at least one action");
    }
    actionCount += binding.actions.size();
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& other = preset.midiBindings[previous];
      if (binding.controlChange == other.controlChange
          && (binding.channel == -1 || other.channel == -1
              || binding.channel == other.channel)) {
        throw std::invalid_argument("MIDI bindings must not overlap on a controller");
      }
    }
    for (const auto& action : binding.actions) {
      const bool targetExists = !action.blockId.empty()
        && (isTopLevelBlockId(preset, action.blockId)
            || (preset.wdw && containsWdwBlockId(*preset.wdw, action.blockId)));
      if (!targetExists) {
        throw std::invalid_argument("MIDI action target block does not exist in the preset");
      }
      if (action.target == PresetMidiTargetType::Parameter && action.parameter.empty()) {
        throw std::invalid_argument("MIDI parameter action requires a parameter");
      }
      if (!std::isfinite(action.value1) || !std::isfinite(action.value2)) {
        throw std::invalid_argument("MIDI action values must be finite");
      }
    }
  }

  for (std::size_t index = 0; index < preset.sceneMidiBindings.size(); ++index) {
    const auto& binding = preset.sceneMidiBindings[index];
    ++actionCount;
    if (!preset.sceneSet) {
      throw std::invalid_argument("scene MIDI actions require a scene set");
    }
    if (binding.channel < -1 || binding.channel > 15 || binding.controlChange > 127) {
      throw std::invalid_argument("scene MIDI binding channel or controller is invalid");
    }
    const auto overlaps = [&](int channel, std::uint8_t controlChange) {
      return binding.controlChange == controlChange
        && (binding.channel == -1 || channel == -1 || binding.channel == channel);
    };
    if (std::any_of(preset.midiBindings.begin(), preset.midiBindings.end(),
                    [&](const PresetMidiBinding& other) {
                      return overlaps(other.channel, other.controlChange);
                    })) {
      throw std::invalid_argument("scene and parameter MIDI bindings must not overlap");
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& other = preset.sceneMidiBindings[previous];
      if (overlaps(other.channel, other.controlChange)) {
        throw std::invalid_argument("scene MIDI bindings must not overlap on a controller");
      }
    }
    if (binding.action == PresetSceneMidiActionType::SelectScene) {
      if (!preset.sceneSet || std::none_of(
            preset.sceneSet->scenes.begin(), preset.sceneSet->scenes.end(),
            [&](const PresetScene& scene) { return scene.id == binding.sceneId; })) {
        throw std::invalid_argument("scene MIDI action must reference an existing scene ID");
      }
    } else if (!binding.sceneId.empty()) {
      throw std::invalid_argument("only direct scene MIDI actions may contain a scene ID");
    }
  }
  if (actionCount > 256) {
    throw std::invalid_argument("a preset can contain at most 256 MIDI action targets");
  }
}

bool validSceneId(std::string_view id)
{
  if (id.empty() || id.size() > 64) return false;
  for (const unsigned char value : id) {
    if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
          || (value >= '0' && value <= '9') || value == '-' || value == '_')) return false;
  }
  return true;
}

std::size_t validUtf8CodePointCount(std::string_view text)
{
  std::size_t count = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index]);
    std::size_t width = 0;
    std::uint32_t value = 0;
    if (first < 0x80U) {
      width = 1;
      value = first;
    } else if ((first & 0xe0U) == 0xc0U) {
      width = 2;
      value = first & 0x1fU;
    } else if ((first & 0xf0U) == 0xe0U) {
      width = 3;
      value = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U) {
      width = 4;
      value = first & 0x07U;
    } else {
      return 0;
    }
    if (index + width > text.size()) return 0;
    for (std::size_t offset = 1; offset < width; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xc0U) != 0x80U) return 0;
      value = (value << 6U) | (next & 0x3fU);
    }
    if ((width == 2 && value < 0x80U)
        || (width == 3 && value < 0x800U)
        || (width == 4 && value < 0x10000U)
        || value > 0x10ffffU
        || (value >= 0xd800U && value <= 0xdfffU)) return 0;
    if (value < 0x20U || (value >= 0x7fU && value <= 0x9fU)) return 0;
    ++count;
    index += width;
  }
  return count;
}

bool validSceneName(const std::string& name)
{
  const auto first = name.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return false;
  const auto last = name.find_last_not_of(" \t\r\n");
  if (first != 0 || last + 1 != name.size()) return false;
  const auto count = validUtf8CodePointCount(name);
  return count >= 1 && count <= 24;
}

bool finiteNumber(const nlohmann::json& value)
{
  return value.is_number() && std::isfinite(value.get<double>());
}

std::string sceneTargetAddress(const PresetSceneTarget& target)
{
  switch (target.target) {
    case PresetSceneTargetType::InputGainDb:
      return "inputGainDb";
    case PresetSceneTargetType::Parameter:
      return "parameter\x1f" + target.blockId + "\x1f" + target.parameter;
    case PresetSceneTargetType::BlockEnabled:
      return "blockEnabled\x1f" + target.blockId;
    case PresetSceneTargetType::WdwLane:
      return "wdwLane\x1f" + target.lane + "\x1f" + target.parameter;
  }
  throw std::invalid_argument("unknown scene target");
}

void validateSceneTarget(const Preset& preset, const PresetSceneTarget& target)
{
  const bool blockExists = !target.blockId.empty()
    && (containsBlockId(preset.blocks, target.blockId)
        || (preset.wdw && containsWdwBlockId(*preset.wdw, target.blockId)));
  switch (target.target) {
    case PresetSceneTargetType::InputGainDb:
      if (!target.blockId.empty() || !target.parameter.empty() || !target.lane.empty()
          || !finiteNumber(target.value)) {
        throw std::invalid_argument("scene input gain target is invalid");
      }
      return;
    case PresetSceneTargetType::Parameter:
      if (!blockExists || target.parameter.empty() || !target.lane.empty()
          || !finiteNumber(target.value)) {
        throw std::invalid_argument("scene parameter target is invalid");
      }
      return;
    case PresetSceneTargetType::BlockEnabled:
      if (!blockExists || !target.parameter.empty() || !target.lane.empty()
          || !target.value.is_boolean()) {
        throw std::invalid_argument("scene block-enabled target is invalid");
      }
      return;
    case PresetSceneTargetType::WdwLane: {
      const bool laneValid = target.lane == "dry" || target.lane == "wet";
      const bool parameterValid = target.parameter == "levelDb"
        || (target.lane == "dry" && target.parameter == "pan")
        || (target.lane == "wet" && target.parameter == "width")
        || target.parameter == "enabled";
      const bool valueValid = target.parameter == "enabled"
        ? target.value.is_boolean() : finiteNumber(target.value);
      if (!preset.wdw || !target.blockId.empty() || !laneValid
          || !parameterValid || !valueValid) {
        throw std::invalid_argument("scene wet/dry/wet lane target is invalid");
      }
      return;
    }
  }
  throw std::invalid_argument("unknown scene target");
}

void validateSceneSet(const Preset& preset)
{
  if (preset.version == 4 && !preset.sceneSet) {
    throw std::invalid_argument("preset version 4 requires a scene set");
  }
  if (preset.version != 4 && preset.sceneSet) {
    throw std::invalid_argument("scenes require preset version 4");
  }
  if (!preset.sceneSet) return;

  const auto& sceneSet = *preset.sceneSet;
  std::set<std::string> ids;
  std::set<std::string> expectedTargets;
  for (std::size_t index = 0; index < sceneSet.scenes.size(); ++index) {
    const auto& scene = sceneSet.scenes[index];
    if (!validSceneId(scene.id) || !ids.insert(scene.id).second) {
      throw std::invalid_argument("scene IDs must be unique safe identifiers");
    }
    if (!validSceneName(scene.name)) {
      throw std::invalid_argument("scene name must contain 1 to 24 characters without surrounding whitespace");
    }
    if (scene.enterTimeMs != 0
        && (scene.enterTimeMs < 100 || scene.enterTimeMs > 10000
            || scene.enterTimeMs % 100 != 0)) {
      throw std::invalid_argument("scene enter time must be instant or 100 to 10000 ms in 100 ms steps");
    }
    if (!std::isfinite(scene.outputTrimDb)
        || scene.outputTrimDb < -12.0f || scene.outputTrimDb > 6.0f) {
      throw std::invalid_argument("scene output trim must be between -12 and 6 dB");
    }
    if (scene.targets.size() > 512) {
      throw std::invalid_argument("a scene can contain at most 512 targets");
    }
    std::set<std::string> addresses;
    for (const auto& target : scene.targets) {
      validateSceneTarget(preset, target);
      if (!addresses.insert(sceneTargetAddress(target)).second) {
        throw std::invalid_argument("scene target addresses must be unique");
      }
    }
    if (index == 0) expectedTargets = std::move(addresses);
    else if (addresses != expectedTargets) {
      throw std::invalid_argument("all scenes must define the same target addresses");
    }
  }
  if (!ids.contains(sceneSet.defaultSceneId)) {
    throw std::invalid_argument("default scene ID must reference one of the four scenes");
  }
}

void validateBlockAssets(const std::vector<PresetBlock>& blocks, int version, bool insideLane = false)
{
  for (const auto& block : blocks) {
    if (block.sceneBypass == PresetSceneBypassPolicy::LetRing
        && (version != 4 || (block.type != "delay" && block.type != "reverb"
                            && block.type != "irreverb"))) {
      throw std::invalid_argument("let-ring scene bypass requires a version 4 delay or reverb block");
    }
    if (!block.asset.empty() && !isValidBlockAssetPath(block.asset)) {
      throw std::invalid_argument("preset asset must stay under data root");
    }
    if (block.type == "dualRig") {
      if (version != 2 && version != 4) {
        throw std::invalid_argument("dual rig requires preset version 2 or 4");
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

void validateWdwLane(const WdwLane& lane, int version, const char* name, bool dry)
{
  if (!std::isfinite(lane.levelDb) || lane.levelDb < -60.0f || lane.levelDb > 12.0f) {
    throw std::invalid_argument(std::string{"WDW "} + name + " lane level must be between -60 and 12 dB");
  }
  if (dry && (!std::isfinite(lane.pan) || lane.pan < -1.0f || lane.pan > 1.0f)) {
    throw std::invalid_argument(std::string{"WDW "} + name + " lane pan must be between -1 and 1");
  }
  if (!dry && (!std::isfinite(lane.pan) || std::fabs(lane.pan) > 1.0e-6f)) {
    throw std::invalid_argument(std::string{"WDW "} + name + " lane does not support pan");
  }
  if (!dry && (!std::isfinite(lane.width) || lane.width < 0.0f || lane.width > 1.0f)) {
    throw std::invalid_argument(std::string{"WDW "} + name + " lane width must be between 0 and 1");
  }
  if (dry && (!std::isfinite(lane.width) || std::fabs(lane.width - 1.0f) > 1.0e-6f)) {
    throw std::invalid_argument(std::string{"WDW "} + name + " lane does not support width");
  }
  validateBlockAssets(lane.blocks, version, true);
}

nlohmann::json blockToJson(const PresetBlock& block, int version)
{
  nlohmann::json json = {
    {"id", block.id},
    {"type", block.type},
    {"enabled", block.enabled},
    {"asset", block.asset},
    {"params", block.params.is_null() ? nlohmann::json::object() : block.params},
  };
  const bool qualifiedSceneBypass = version == 4
    && (block.type == "delay" || block.type == "reverb" || block.type == "irreverb");
  if (qualifiedSceneBypass) {
    json["sceneBypass"] = block.sceneBypass == PresetSceneBypassPolicy::LetRing
      ? "letRing" : "cut";
  }
  if (block.type == "dualRig") {
    nlohmann::json left = nlohmann::json::array();
    nlohmann::json right = nlohmann::json::array();
    for (const auto& child : block.lanes[0]) left.push_back(blockToJson(child, version));
    for (const auto& child : block.lanes[1]) right.push_back(blockToJson(child, version));
    json["lanes"] = {
      {"left", {{"blocks", std::move(left)}}},
      {"right", {{"blocks", std::move(right)}}},
    };
  }
  return json;
}

PresetBlock blockFromJson(const nlohmann::json& json, bool insideLane, int version)
{
  PresetBlock block;
  block.id = json.at("id").get<std::string>();
  block.type = json.at("type").get<std::string>();
  block.enabled = json.value("enabled", true);
  const bool qualifiedLetRing = version == 4
    && (block.type == "delay" || block.type == "reverb" || block.type == "irreverb");
  const auto sceneBypass = json.value("sceneBypass", qualifiedLetRing ? "letRing" : "cut");
  if (sceneBypass == "cut") block.sceneBypass = PresetSceneBypassPolicy::Cut;
  else if (sceneBypass == "letRing") block.sceneBypass = PresetSceneBypassPolicy::LetRing;
  else throw std::invalid_argument("scene bypass must be cut or letRing");
  block.asset = json.value("asset", "");
  block.params = json.value("params", nlohmann::json::object());
  normalizeLegacyEffectBlock(block);
  if (block.type == "dualRig") {
    if (insideLane) {
      throw std::invalid_argument("nested dual rig blocks are not supported");
    }
    const auto& lanes = json.at("lanes");
    for (const auto& child : lanes.at("left").at("blocks")) {
      block.lanes[0].push_back(blockFromJson(child, true, version));
    }
    for (const auto& child : lanes.at("right").at("blocks")) {
      block.lanes[1].push_back(blockFromJson(child, true, version));
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

nlohmann::json sceneTargetToJson(const PresetSceneTarget& target)
{
  nlohmann::json json = {{"value", target.value}};
  switch (target.target) {
    case PresetSceneTargetType::InputGainDb:
      json["target"] = "inputGainDb";
      break;
    case PresetSceneTargetType::Parameter:
      json["target"] = "parameter";
      json["blockId"] = target.blockId;
      json["parameter"] = target.parameter;
      break;
    case PresetSceneTargetType::BlockEnabled:
      json["target"] = "blockEnabled";
      json["blockId"] = target.blockId;
      break;
    case PresetSceneTargetType::WdwLane:
      json["target"] = "wdwLane";
      json["lane"] = target.lane;
      json["parameter"] = target.parameter;
      break;
  }
  return json;
}

PresetSceneTarget sceneTargetFromJson(const nlohmann::json& json)
{
  if (!json.is_object() || !json.contains("value")) {
    throw std::invalid_argument("scene target must be an object with a value");
  }
  PresetSceneTarget result;
  const auto type = json.at("target").get<std::string>();
  if (type == "inputGainDb") result.target = PresetSceneTargetType::InputGainDb;
  else if (type == "parameter") result.target = PresetSceneTargetType::Parameter;
  else if (type == "blockEnabled") result.target = PresetSceneTargetType::BlockEnabled;
  else if (type == "wdwLane") result.target = PresetSceneTargetType::WdwLane;
  else throw std::invalid_argument("unknown scene target type");
  result.blockId = json.value("blockId", "");
  result.parameter = json.value("parameter", "");
  result.lane = json.value("lane", "");
  result.value = json.at("value");
  return result;
}

nlohmann::json sceneSetToJson(const PresetSceneSet& sceneSet)
{
  nlohmann::json scenes = nlohmann::json::array();
  for (const auto& scene : sceneSet.scenes) {
    nlohmann::json targets = nlohmann::json::array();
    for (const auto& target : scene.targets) targets.push_back(sceneTargetToJson(target));
    scenes.push_back({
      {"id", scene.id},
      {"name", scene.name},
      {"enterTimeMs", scene.enterTimeMs},
      {"outputTrimDb", scene.outputTrimDb},
      {"targets", std::move(targets)},
    });
  }
  return {
    {"defaultSceneId", sceneSet.defaultSceneId},
    {"openIn", sceneSet.openIn == PresetSceneOpenMode::Scenes ? "scenes" : "presets"},
    {"scenes", std::move(scenes)},
  };
}

PresetSceneSet sceneSetFromJson(const nlohmann::json& json)
{
  if (!json.is_object() || !json.contains("scenes") || !json.at("scenes").is_array()
      || json.at("scenes").size() != 4) {
    throw std::invalid_argument("scene set requires exactly four scenes");
  }
  PresetSceneSet result;
  result.defaultSceneId = json.at("defaultSceneId").get<std::string>();
  const auto openIn = json.at("openIn").get<std::string>();
  if (openIn == "scenes") result.openIn = PresetSceneOpenMode::Scenes;
  else if (openIn == "presets") result.openIn = PresetSceneOpenMode::Presets;
  else throw std::invalid_argument("scene set openIn must be presets or scenes");
  for (std::size_t index = 0; index < result.scenes.size(); ++index) {
    const auto& sceneJson = json.at("scenes").at(index);
    if (!sceneJson.is_object() || !sceneJson.contains("targets")
        || !sceneJson.at("targets").is_array()) {
      throw std::invalid_argument("scene must contain a targets array");
    }
    auto& scene = result.scenes[index];
    scene.id = sceneJson.at("id").get<std::string>();
    scene.name = sceneJson.at("name").get<std::string>();
    const auto enterTime = sceneJson.at("enterTimeMs").get<int>();
    if (enterTime < 0) throw std::invalid_argument("scene enter time cannot be negative");
    scene.enterTimeMs = static_cast<std::uint32_t>(enterTime);
    scene.outputTrimDb = sceneJson.at("outputTrimDb").get<float>();
    for (const auto& targetJson : sceneJson.at("targets")) {
      scene.targets.push_back(sceneTargetFromJson(targetJson));
    }
  }
  return result;
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
  if (preset.version < 1 || preset.version > 4) {
    throw std::invalid_argument("preset version must be 1, 2, 3, or 4");
  }
  requireRouting(preset.routing);
  if (preset.routing == "serial" && preset.version == 3) {
    throw std::invalid_argument("wet/dry/wet routing requires preset version 3");
  }
  if (preset.routing == "wdw" && preset.version != 3 && preset.version != 4) {
    throw std::invalid_argument("wet/dry/wet routing requires preset version 3 or 4");
  }
  if (preset.routing == "serial" && preset.wdw) {
    throw std::invalid_argument("serial presets cannot contain wet/dry/wet lanes");
  }
  if (preset.routing == "wdw" && !preset.wdw) {
    throw std::invalid_argument("wet/dry/wet preset requires dry and wet lanes");
  }
  validateBlockAssets(preset.blocks, preset.version);
  if (preset.wdw) {
    if (!preset.blocks.empty()) {
      throw std::invalid_argument("wet/dry/wet presets must keep top-level blocks empty");
    }
    validateWdwLane(preset.wdw->dry, preset.version, "dry", true);
    validateWdwLane(preset.wdw->wet, preset.version, "wet", false);
  }
  validateExpression(preset);
  validateMidiBindings(preset);
  validateSceneSet(preset);

  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& block : preset.blocks) {
    blocks.push_back(blockToJson(block, preset.version));
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
  if (preset.wdw) {
    auto laneToJson = [version = preset.version](const WdwLane& lane, bool dry) {
      nlohmann::json laneBlocks = nlohmann::json::array();
      for (const auto& block : lane.blocks) laneBlocks.push_back(blockToJson(block, version));
      nlohmann::json result = {
        {"blocks", std::move(laneBlocks)},
        {"levelDb", lane.levelDb},
        {"enabled", lane.enabled},
      };
      if (dry) result["pan"] = lane.pan;
      else result["width"] = lane.width;
      return result;
    };
    json["wdw"] = {
      {"dry", laneToJson(preset.wdw->dry, true)},
      {"wet", laneToJson(preset.wdw->wet, false)},
    };
  }
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
  if (!preset.sceneMidiBindings.empty()) {
    json["sceneMidiMappings"] = nlohmann::json::array();
    for (const auto& binding : preset.sceneMidiBindings) {
      const char* action = "selectScene";
      switch (binding.action) {
        case PresetSceneMidiActionType::SelectScene: action = "selectScene"; break;
        case PresetSceneMidiActionType::SceneNumber: action = "sceneNumber"; break;
        case PresetSceneMidiActionType::ShowPresets: action = "showPresets"; break;
        case PresetSceneMidiActionType::ShowScenes: action = "showScenes"; break;
      }
      nlohmann::json mapping = {
        {"channel", binding.channel},
        {"controlChange", binding.controlChange},
        {"action", action},
      };
      if (binding.action == PresetSceneMidiActionType::SelectScene) {
        mapping["sceneId"] = binding.sceneId;
      }
      json["sceneMidiMappings"].push_back(std::move(mapping));
    }
  }
  if (preset.sceneSet) json["sceneSet"] = sceneSetToJson(*preset.sceneSet);
  return json;
}

Preset presetFromJson(const nlohmann::json& json)
{
  Preset preset;
  preset.version = json.at("version").get<int>();
  if (preset.version < 1 || preset.version > 4) {
    throw std::invalid_argument("preset version must be 1, 2, 3, or 4");
  }
  preset.name = json.value("name", "");
  preset.routing = json.at("routing").get<std::string>();
  requireRouting(preset.routing);
  if (preset.routing == "serial" && preset.version == 3) {
    throw std::invalid_argument("wet/dry/wet routing requires preset version 3");
  }
  if (preset.routing == "wdw" && preset.version != 3 && preset.version != 4) {
    throw std::invalid_argument("wet/dry/wet routing requires preset version 3 or 4");
  }

  const auto& global = json.at("global");
  preset.global.inputGainDb = global.value("inputGainDb", 0.0f);
  preset.global.outputGainDb = global.value("outputGainDb", 0.0f);
  preset.global.safetyLimitDb = global.value("safetyLimitDb", -1.0f);

  for (const auto& blockJson : json.at("blocks")) {
    preset.blocks.push_back(blockFromJson(blockJson, false, preset.version));
  }

  validateBlockAssets(preset.blocks, preset.version);
  if (preset.routing == "wdw" && !preset.blocks.empty()) {
    throw std::invalid_argument("wet/dry/wet presets must keep top-level blocks empty");
  }
  if (const auto wdw = json.find("wdw"); wdw != json.end() && !wdw->is_null()) {
    if (preset.routing != "wdw") {
      throw std::invalid_argument("serial presets cannot contain wet/dry/wet lanes");
    }
    if (!wdw->is_object() || !wdw->contains("dry") || !wdw->contains("wet")) {
      throw std::invalid_argument("wet/dry/wet preset requires dry and wet lanes");
    }
    auto parseLane = [&](const nlohmann::json& laneJson, const char* name, bool dry) {
      if (!laneJson.is_object() || !laneJson.contains("blocks")
          || !laneJson.at("blocks").is_array()) {
        throw std::invalid_argument(std::string{"WDW "} + name + " lane requires a blocks array");
      }
      WdwLane lane;
      lane.levelDb = laneJson.value("levelDb", 0.0f);
      lane.pan = laneJson.value("pan", 0.0f);
      lane.width = laneJson.value("width", 1.0f);
      lane.enabled = laneJson.value("enabled", true);
      for (const auto& child : laneJson.at("blocks")) {
        lane.blocks.push_back(blockFromJson(child, true, preset.version));
      }
      validateWdwLane(lane, preset.version, name, dry);
      return lane;
    };
    preset.wdw = WdwRouting{
      parseLane(wdw->at("dry"), "dry", true),
      parseLane(wdw->at("wet"), "wet", false),
    };
  }
  if (preset.routing == "wdw" && !preset.wdw) {
    throw std::invalid_argument("wet/dry/wet preset requires dry and wet lanes");
  }
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
  if (const auto sceneSet = json.find("sceneSet");
      sceneSet != json.end() && !sceneSet->is_null()) {
    preset.sceneSet = sceneSetFromJson(*sceneSet);
  }
  validateSceneSet(preset);

  if (const auto mappings = json.find("sceneMidiMappings");
      mappings != json.end() && !mappings->is_null()) {
    if (!mappings->is_array()) {
      throw std::invalid_argument("scene MIDI mappings must be an array");
    }
    for (const auto& bindingJson : *mappings) {
      PresetSceneMidiBinding binding;
      binding.channel = bindingJson.value("channel", -1);
      const int controlChange = bindingJson.at("controlChange").get<int>();
      if (controlChange < 0 || controlChange > 127) {
        throw std::invalid_argument("scene MIDI controller must be between 0 and 127");
      }
      binding.controlChange = static_cast<std::uint8_t>(controlChange);
      const auto action = bindingJson.at("action").get<std::string>();
      if (action == "selectScene") binding.action = PresetSceneMidiActionType::SelectScene;
      else if (action == "sceneNumber") binding.action = PresetSceneMidiActionType::SceneNumber;
      else if (action == "showPresets") binding.action = PresetSceneMidiActionType::ShowPresets;
      else if (action == "showScenes") binding.action = PresetSceneMidiActionType::ShowScenes;
      else throw std::invalid_argument("unknown scene MIDI action");
      binding.sceneId = bindingJson.value("sceneId", "");
      preset.sceneMidiBindings.push_back(std::move(binding));
    }
  }
  validateMidiBindings(preset);

  return preset;
}

bool flattenPresetScene(const Preset& source, std::size_t sceneIndex,
                        Preset& flattened, std::string& error)
{
  if (!source.sceneSet || sceneIndex >= source.sceneSet->scenes.size()) {
    error = "scene flatten requires one of the preset's four scenes";
    return false;
  }
  try {
    // Validate the complete source before resolving self-describing target
    // addresses. Imported invalid scene data must fail instead of disappearing.
    (void)toJson(source);
    flattened = source;
    const auto& scene = source.sceneSet->scenes[sceneIndex];
    const float outputGain = source.global.outputGainDb + scene.outputTrimDb;
    if (!std::isfinite(outputGain) || outputGain < -60.0f || outputGain > 12.0f) {
      error = "scene trim cannot fit the preset output range";
      return false;
    }
    flattened.global.outputGainDb = outputGain;

    const auto findBlock = [&](const auto& self, std::vector<PresetBlock>& blocks,
                               const std::string& id) -> PresetBlock* {
      for (auto& block : blocks) {
        if (block.id == id) return &block;
        for (auto& lane : block.lanes) {
          if (auto* child = self(self, lane, id)) return child;
        }
      }
      return nullptr;
    };
    for (const auto& target : scene.targets) {
      if (target.target == PresetSceneTargetType::InputGainDb) {
        flattened.global.inputGainDb = target.value.get<float>();
        continue;
      }
      if (target.target == PresetSceneTargetType::WdwLane) {
        auto& lane = target.lane == "dry" ? flattened.wdw->dry : flattened.wdw->wet;
        if (target.parameter == "enabled") lane.enabled = target.value.get<bool>();
        else if (target.parameter == "levelDb") lane.levelDb = target.value.get<float>();
        else if (target.parameter == "pan") lane.pan = target.value.get<float>();
        else if (target.parameter == "width") lane.width = target.value.get<float>();
        continue;
      }
      PresetBlock* block = findBlock(findBlock, flattened.blocks, target.blockId);
      if (!block && flattened.wdw)
        block = findBlock(findBlock, flattened.wdw->dry.blocks, target.blockId);
      if (!block && flattened.wdw)
        block = findBlock(findBlock, flattened.wdw->wet.blocks, target.blockId);
      if (!block) {
        error = "scene target block was not found while flattening: " + target.blockId;
        return false;
      }
      if (target.target == PresetSceneTargetType::BlockEnabled)
        block->enabled = target.value.get<bool>();
      else block->params[target.parameter] = target.value;
    }

    const auto clearScenePolicy = [&](const auto& self,
                                      std::vector<PresetBlock>& blocks) -> void {
      for (auto& block : blocks) {
        block.sceneBypass = PresetSceneBypassPolicy::Cut;
        for (auto& lane : block.lanes) self(self, lane);
      }
    };
    clearScenePolicy(clearScenePolicy, flattened.blocks);
    if (flattened.wdw) {
      clearScenePolicy(clearScenePolicy, flattened.wdw->dry.blocks);
      clearScenePolicy(clearScenePolicy, flattened.wdw->wet.blocks);
    }
    flattened.sceneSet.reset();
    flattened.sceneMidiBindings.clear();
    const auto hasDualRig = [&](const auto& self,
                                const std::vector<PresetBlock>& blocks) -> bool {
      for (const auto& block : blocks) {
        if (block.type == "dualRig") return true;
        for (const auto& lane : block.lanes) if (self(self, lane)) return true;
      }
      return false;
    };
    flattened.version = flattened.wdw ? 3
      : hasDualRig(hasDualRig, flattened.blocks) ? 2 : 1;
    (void)toJson(flattened);
    error.clear();
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
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
