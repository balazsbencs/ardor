#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
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

// Version-3 wet/dry/wet presets keep the two complete signal lanes explicit.
// The dry lane contributes a mono signal with an equal-power pan; the wet lane
// preserves stereo and exposes a width control.  These are persisted in dB so
// the preset format stays friendly to the manager and device editors.
struct WdwLane {
  std::vector<PresetBlock> blocks;
  float levelDb = 0.0f;
  // Used by the mono dry contribution. Wet remains a stereo pair and uses
  // width instead of a whole-lane pan control. Dry width is fixed at 1.
  float pan = 0.0f;
  float width = 1.0f;
  bool enabled = true;
};

struct WdwRouting {
  WdwLane dry;
  WdwLane wet;
};

struct PresetExpression {
  // Stable block ID plus the block parameter key avoids binding a preset to
  // the block's current position in the signal chain.
  std::string blockId;
  std::string parameter;
  float minimum = 0.0f;
  float maximum = 1.0f;
  bool inverted = false;
};

enum class PresetMidiBindingMode {
  Continuous,
  Toggle,
};

enum class PresetMidiTargetType {
  Parameter,
  BlockEnabled,
};

// A MIDI action always has two endpoints. Continuous bindings interpolate
// between them from CC 0..127. Toggle bindings apply the complete set of
// value1 endpoints for scene 1 and value2 endpoints for scene 2.
struct PresetMidiAction {
  PresetMidiTargetType target = PresetMidiTargetType::Parameter;
  std::string blockId;
  std::string parameter;
  float value1 = 0.0f;
  float value2 = 1.0f;
};

struct PresetMidiBinding {
  // -1 is channel-omni; 0..15 selects a MIDI channel.
  int channel = -1;
  std::uint8_t controlChange = 0;
  PresetMidiBindingMode mode = PresetMidiBindingMode::Continuous;
  std::vector<PresetMidiAction> actions;
};

struct Preset {
  int version = 1;
  std::string name;
  std::string routing = "serial";
  PresetGlobal global;
  std::vector<PresetBlock> blocks;
  std::optional<WdwRouting> wdw;
  std::optional<PresetExpression> expression;
  std::vector<PresetMidiBinding> midiBindings;
};

nlohmann::json toJson(const Preset& preset);
Preset presetFromJson(const nlohmann::json& json);
bool isValidBlockAssetPath(std::string_view asset);
float expressionValueAt(const PresetExpression& assignment, float normalizedPosition);
float midiActionValueAt(const PresetMidiAction& action, std::uint8_t controlValue);

} // namespace ardor
