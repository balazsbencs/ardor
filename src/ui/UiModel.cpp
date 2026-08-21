#include "ui/UiModel.h"

#include "daisyfx/DaisyFxCatalog.h"
#include "ui/ParameterControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace ardor {

void markUiChanged(UiState& state, UiChange changes)
{
  if (hasUiChange(changes, UiChange::Navigation)) ++state.revisions.navigation;
  if (hasUiChange(changes, UiChange::Header)) ++state.revisions.header;
  if (hasUiChange(changes, UiChange::Presets)) ++state.revisions.presets;
  if (hasUiChange(changes, UiChange::Chain)) ++state.revisions.chain;
  if (hasUiChange(changes, UiChange::Parameters)) ++state.revisions.parameters;
  if (hasUiChange(changes, UiChange::Assets)) ++state.revisions.assets;
  if (hasUiChange(changes, UiChange::Drawers)) ++state.revisions.drawers;
  if (hasUiChange(changes, UiChange::Status)) ++state.revisions.status;
  if (hasUiChange(changes, UiChange::Telemetry)) ++state.revisions.telemetry;
}

namespace {

std::string labelForBlockType(const std::string& type)
{
  if (type == "nam") {
    return "Neural Amp";
  }
  if (type == "cab") {
    return "Cab";
  }
  if (type == "dualAmp") {
    return "Dual Amp";
  }
  if (type == "dualRig") {
    return "Dual Rig";
  }
  if (type == "mod") {
    return "Modulation";
  }
  if (type == "modulation") {
    return "Modulation";
  }
  if (type == "delay") {
    return "Delay";
  }
  if (type == "reverb") {
    return "Reverb";
  }
  if (type == "time") {
    return "Time";
  }
  if (type == "dynamics") {
    return "Dynamics";
  }
  if (type == "eq") {
    return "EQ";
  }
  if (type == "wah") {
    return "Wah";
  }
  if (type == "distortion") {
    return "Drive";
  }
  if (type == "irreverb") {
    return "Reverb";
  }
  if (type == "stereo") {
    return "Stereo";
  }
  return type;
}

std::string categoryForDaisyKind(DaisyFxKind kind)
{
  switch (kind) {
  case DaisyFxKind::Mod:
    return "modulation";
  case DaisyFxKind::Delay:
    return "delay";
  case DaisyFxKind::Reverb:
    return "reverb";
  }
  return "all";
}

std::string assetNameForPath(const UiState& state, const std::string& path, const std::string& type)
{
  if (path.empty()) {
    return type;
  }
  for (const auto& asset : state.assets) {
    if (asset.path == path) {
      return asset.name;
    }
  }
  return path.empty() ? type : path;
}

std::string assetNameForBlock(const UiState& state, const PresetBlock& block)
{
  if (block.type == "dualRig") {
    return "Left " + std::to_string(block.lanes[0].size())
      + " blocks  /  Right " + std::to_string(block.lanes[1].size()) + " blocks";
  }
  if (block.type == "dualAmp") {
    const auto left = block.params.value("leftNamAsset", std::string{});
    const auto right = block.params.value("rightNamAsset", std::string{});
    const auto shortName = [](const std::string& path) {
      return path.empty() ? std::string{"Choose model"} : std::filesystem::path{path}.filename().string();
    };
    return "L " + shortName(left) + " / R " + shortName(right);
  }
  if (const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""))) {
    return descriptor->name;
  }
  if (block.type == "dynamics" && block.params.value("mode", "") == "compressor") {
    return "Compressor";
  }
  if (block.type == "dynamics" && block.params.value("mode", "") == "noise_gate") {
    return "Noise Gate";
  }
  if (block.type == "eq" && isParametricEqMode(block.params)) {
    return "Five Band EQ";
  }
  if (block.type == "dynamics" && block.params.value("mode", "") == "transient_shaper") {
    return "Transient Shaper";
  }
  if (block.type == "wah" && block.params.value("mode", "") == "gcb95") {
    return "GCB-95 Wah";
  }
  if (block.type == "stereo") {
    return "Stereo Widener";
  }
  if (block.type == "distortion" && block.params.value("mode", "") == "rat") {
    return "RAT Distortion";
  }
  if (block.type == "distortion" && block.params.value("mode", "") == "tape") {
    return "Tape Machine";
  }
  if (block.type == "distortion" && block.params.value("mode", "") == "big_cheese") {
    return "Big Cheese Fuzz";
  }
  return assetNameForPath(state, block.asset, block.type);
}

float clampFloat(float value, float low, float high)
{
  return std::clamp(value, low, high);
}

nlohmann::json defaultCompressorParams()
{
  return {
    {"mode", "compressor"},
    {"threshold_db", -24.0f},
    {"ratio", 4.0f},
    {"attack_ms", 10.0f},
    {"release_ms", 150.0f},
    {"knee_db", 6.0f},
    {"makeup_db", 0.0f},
    {"input_gain_db", 0.0f},
    {"mix", 1.0f},
    {"sidechain_hpf_hz", 80.0f},
    {"detector", "peak"},
    {"auto_makeup", false},
  };
}

nlohmann::json defaultNoiseGateParams()
{
  return {
    {"mode", "noise_gate"},
    {"threshold_db", -55.0f},
    {"reduction_db", 80.0f},
    {"attack_ms", 2.0f},
    {"hold_ms", 50.0f},
    {"release_ms", 150.0f},
    {"hysteresis_db", 6.0f},
    {"sidechain_hpf_hz", 80.0f},
  };
}

nlohmann::json defaultTransientShaperParams()
{
  return {
    {"mode", "transient_shaper"},
    {"attack", 0.0f},
    {"sustain", 0.0f},
    {"mix", 1.0f},
    {"output_db", 0.0f},
  };
}

nlohmann::json defaultTapeParams()
{
  return {
    {"mode", "tape"},
    {"drive", 0.0f},
    {"saturation", 0.5f},
    {"bias", 0.5f},
    {"speed", "15"},
    {"head_bump", 0.5f},
    // Flutter and hiss ship inert. Adding tape saturation should give
    // saturation; the pitch movement and the noise floor are reached for
    // deliberately.
    {"flutter", 0.0f},
    {"hiss_db", -120.0f},
    {"mix", 1.0f},
    {"output_db", 0.0f},
  };
}

nlohmann::json defaultRatParams()
{
  return {
    {"mode", "rat"},
    {"distortion", 0.5f},
    {"filter", 0.5f},
    {"volume", 0.7f},
  };
}

nlohmann::json defaultCheeseParams()
{
  return {
    {"mode", "big_cheese"},
    {"fuzz", 0.7f},
    {"tone", 0.5f},
    {"volume", 0.7f},
  };
}

nlohmann::json defaultStereoWidenerParams()
{
  return {
    {"width", 1.0f},
    {"delayMs", 0.0f},
    {"bassMonoHz", 0.0f},
    {"levelDb", 0.0f},
  };
}

nlohmann::json defaultIrReverbParams()
{
  return {
    {"mix", 0.35f},
    {"levelDb", 0.0f},
    {"preDelayMs", 0.0f},
    {"lowCutHz", 20.0f},
    {"highCutHz", 20000.0f},
  };
}

nlohmann::json paramsWithKnownDefaults(const std::string& type, const nlohmann::json& supplied)
{
  nlohmann::json params = supplied.is_object() ? supplied : nlohmann::json::object();
  nlohmann::json defaults = nlohmann::json::object();
  if (const auto* descriptor = findDaisyFxDescriptor(type, params.value("mode", ""))) {
    defaults = defaultDaisyFxParams(*descriptor);
  } else if (type == "nam" && !params.contains("quality")) {
    defaults = {{"inputMode", "sum"}, {"useNano", false}};
  } else if (type == "dualAmp") {
    defaults = {
      {"inputMode", "sum"},
      {"leftNamAsset", ""}, {"leftUseNano", false},
      {"leftIrAsset", ""}, {"leftCabLevelDb", 0.0f}, {"leftCabMix", 1.0f},
      {"leftPolarityInvert", false},
      {"rightNamAsset", ""}, {"rightUseNano", false},
      {"rightIrAsset", ""}, {"rightCabLevelDb", 0.0f}, {"rightCabMix", 1.0f},
      {"rightPolarityInvert", false},
    };
  } else if (type == "dualRig") {
    defaults = {
      {"inputMode", "sum"},
      {"leftLevelDb", 0.0f}, {"leftPolarityInvert", false},
      {"rightLevelDb", 0.0f}, {"rightPolarityInvert", false},
    };
  } else if (type == "dynamics" && params.value("mode", "") == "compressor") {
    defaults = defaultCompressorParams();
  } else if (type == "dynamics" && params.value("mode", "") == "noise_gate") {
    defaults = defaultNoiseGateParams();
  } else if (type == "dynamics" && params.value("mode", "") == "transient_shaper") {
    defaults = defaultTransientShaperParams();
  } else if (type == "distortion" && params.value("mode", std::string{"rat"}) == "rat") {
    defaults = defaultRatParams();
  } else if (type == "distortion" && params.value("mode", "") == "tape") {
    defaults = defaultTapeParams();
  } else if (type == "distortion" && params.value("mode", "") == "big_cheese") {
    defaults = defaultCheeseParams();
  } else if (type == "stereo") {
    defaults = defaultStereoWidenerParams();
  } else if (type == "irreverb") {
    defaults = defaultIrReverbParams();
  } else if (type == "eq" && isParametricEqMode(params)) {
    return parametricEqParamsToJson(parametricEqParamsFromJson(params));
  } else if (type == "wah" && params.value("mode", std::string{"gcb95"}) == "gcb95") {
    defaults = {{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}};
  }
  for (auto it = defaults.begin(); it != defaults.end(); ++it) {
    if (!params.contains(it.key())) {
      params[it.key()] = it.value();
    }
  }
  return params;
}

std::string nextBlockId(const std::vector<UiBlock>& blocks)
{
  int maxId = 0;
  const auto inspect = [&](const auto& self, const UiBlock& block) -> void {
    if (block.id.rfind("block-", 0) != 0) {
      for (const auto& lane : block.lanes) {
        for (const auto& child : lane) self(self, child);
      }
      return;
    }
    try {
      maxId = std::max(maxId, std::stoi(block.id.substr(6)));
    } catch (const std::exception&) {
    }
    for (const auto& lane : block.lanes) {
      for (const auto& child : lane) self(self, child);
    }
  };
  for (const auto& block : blocks) {
    inspect(inspect, block);
  }
  return "block-" + std::to_string(maxId + 1);
}

UiBlock blockFromAsset(const UiState& state, const UiAsset& asset,
                       const std::vector<UiBlock>& allBlocks)
{
  std::string type = asset.type;
  std::string label = asset.name;
  nlohmann::json params = nlohmann::json::object();
  if (asset.type == "amps") {
    type = "nam";
    label = "Neural Amp";
  } else if (asset.type == "cabs") {
    type = "cab";
    label = "Cab";
  } else if (!asset.blockType.empty()) {
    type = asset.blockType;
    label = labelForBlockType(asset.blockType);
    if (const auto* descriptor = findDaisyFxDescriptor(asset.blockType, asset.mode)) {
      params = defaultDaisyFxParams(*descriptor);
    } else if (asset.blockType == "dynamics" && asset.mode == "compressor") {
      params = defaultCompressorParams();
    } else if (asset.blockType == "dynamics" && asset.mode == "noise_gate") {
      params = defaultNoiseGateParams();
    } else if (asset.blockType == "dynamics" && asset.mode == "transient_shaper") {
      params = defaultTransientShaperParams();
    } else if (asset.blockType == "distortion" && asset.mode == "rat") {
      params = defaultRatParams();
    } else if (asset.blockType == "distortion" && asset.mode == "tape") {
      params = defaultTapeParams();
    } else if (asset.blockType == "distortion" && asset.mode == "big_cheese") {
      params = defaultCheeseParams();
    } else if (asset.blockType == "stereo") {
      params = defaultStereoWidenerParams();
    } else if (asset.blockType == "irreverb") {
      params = defaultIrReverbParams();
    } else if (asset.blockType == "eq" && asset.mode == "parametric_eq_5") {
      params = parametricEqParamsToJson(defaultParametricEqParams());
    } else if (asset.blockType == "wah" && asset.mode == "gcb95") {
      params = {{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}};
    }
  }
  params = paramsWithKnownDefaults(type, params);
  return {nextBlockId(allBlocks), type, label, asset.name, asset.path, true, params};
}

UiBlock makeEmptyDualRig(const std::vector<UiBlock>& allBlocks)
{
  const auto rigId = nextBlockId(allBlocks);
  UiBlock rig{rigId, "dualRig", "Dual Rig", "Left 0 blocks  /  Right 0 blocks", "", true,
              paramsWithKnownDefaults("dualRig", {})};
  return rig;
}

std::string bankName(int bank)
{
  std::ostringstream out;
  out << "Bank " << std::setw(3) << std::setfill('0') << bank;
  return out.str();
}

UiPreset emptyPreset(std::size_t index)
{
  return {"Empty " + std::to_string(index + 1), {}};
}

void rememberBlockEdit(UiState& state)
{
  state.blockEditUndo = UiBlockEditSnapshot{
    state.bank.presets[state.activePreset].blocks,
    state.bank.presets[state.activePreset].expression,
    state.bank.presets[state.activePreset].midiBindings,
    state.selectedBlock,
    state.selectedBlockId,
    state.paramTarget,
    state.dirty,
    state.blockDrawerOpen,
    state.paramDrawerOpen,
  };
}

UiPreviewSnapshot previewSnapshot(const UiState& state)
{
  return {state.bank.presets[state.activePreset], state.selectedBlock, state.selectedBlockId,
          state.paramTarget,
          state.dirty, state.blockDrawerOpen, state.paramDrawerOpen, state.blockEditUndo};
}

void appendAssetsFrom(UiState& state, const std::filesystem::path& dir, const std::string& ext,
                      const std::string& type, const std::string& subtitle,
                      const std::string& blockType = {})
{
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::exists(dir, ec) || ec) {
    return;
  }

  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    std::error_code entryEc;
    if (!entry.is_regular_file(entryEc) || entryEc || entry.path().extension() != ext) {
      continue;
    }
    const auto relativePath = fs::relative(entry.path(), dir.parent_path(), entryEc);
    if (entryEc) {
      continue;
    }
    const auto relative = relativePath.generic_string();
    state.assets.push_back({entry.path().stem().string(), relative, type, blockType, "", subtitle});
  }
}

std::string daisyFamilyLabel(DaisyFxKind kind)
{
  switch (kind) {
  case DaisyFxKind::Mod: return "Modulation";
  case DaisyFxKind::Delay: return "Delay";
  case DaisyFxKind::Reverb: return "Reverb";
  }
  return "";
}

std::string daisySubtitle(const DaisyFxDescriptor& descriptor)
{
  return daisyFamilyLabel(descriptor.kind) + " · " + std::to_string(descriptor.params.size())
    + " controls";
}

void appendDriveAssets(UiState& state)
{
  state.assets.push_back({"RAT Distortion", "", "drive", "distortion", "rat",
                          "Drive · ProCo RAT circuit"});
  state.assets.push_back({"Big Cheese Fuzz", "", "drive", "distortion", "big_cheese",
                          "Drive · Lovetone Big Cheese circuit"});
  state.assets.push_back({"Tape Machine", "", "drive", "distortion", "tape",
                          "Drive · Studer A800 tape machine"});
}

void appendUtilityAssets(UiState& state)
{
  state.assets.push_back({"Compressor", "", "utility", "dynamics", "compressor", "Utility · dynamics"});
  state.assets.push_back({"Noise Gate", "", "utility", "dynamics", "noise_gate", "Utility · dynamics"});
  state.assets.push_back({"Transient Shaper", "", "utility", "dynamics", "transient_shaper",
                          "Utility · attack and sustain shaping"});
  state.assets.push_back({"Five Band EQ", "", "utility", "eq", "parametric_eq_5", "Utility · HPF + 5 bands + LPF"});
  state.assets.push_back({"GCB-95 Wah", "", "utility", "wah", "gcb95", "Utility · expression-controlled wah"});
  state.assets.push_back({"Stereo Widener", "", "utility", "stereo", "", "Utility · mid/side width"});
}

void appendDaisyAssets(UiState& state)
{
  for (const auto& descriptor : daisyFxCatalog()) {
    state.assets.push_back({descriptor.name, "", categoryForDaisyKind(descriptor.kind),
                            descriptor.blockType, descriptor.mode, daisySubtitle(descriptor)});
  }
}

} // namespace

UiState makeDemoUiState()
{
  UiState state;
  state.bank.name = "Bank 000 - Core Sounds";
  state.bank.presets = {
    UiPreset{"Clean Lead", {{"block-1", "nam", "Neural Amp", "Clean Twin", "models/clean.nam", true},
                            {"block-2", "cab", "Cab", "Open Back 2x12", "irs/open-back.wav", true}}},
    UiPreset{"Crunch", {{"block-3", "nam", "Neural Amp", "British Crunch", "models/crunch.nam", true},
                        {"block-4", "cab", "Cab", "Vintage 4x12", "irs/vintage.wav", true}}},
    UiPreset{"Ambient", {{"block-5", "cab", "Cab", "Open Back 2x12", "irs/open-back.wav", true},
                         {"block-6", "mod", "Modulation", "Chorus", "", true,
                          paramsWithKnownDefaults("mod", {{"mode", "chorus"}})},
                         {"block-7", "delay", "Delay", "Tape Delay", "", true,
                          paramsWithKnownDefaults("delay", {{"mode", "tape"}})}}},
    UiPreset{"Solo", {{"block-8", "nam", "Neural Amp", "Focused Lead", "models/solo.nam", true},
                      {"block-9", "cab", "Cab", "Focused 1x12", "irs/focus.wav", true}}},
  };
  state.assets = {
    {"Clean Twin", "models/clean.nam", "amps", "", "", "Amp · neural capture"},
    {"British Crunch", "models/crunch.nam", "amps", "", "", "Amp · neural capture"},
    {"Focused Lead", "models/solo.nam", "amps", "", "", "Amp · neural capture"},
    {"Open Back 2x12", "irs/open-back.wav", "cabs", "", "", "Cab · impulse response"},
    {"Vintage 4x12", "irs/vintage.wav", "cabs", "", "", "Cab · impulse response"},
    {"Focused 1x12", "irs/focus.wav", "cabs", "", "", "Cab · impulse response"},
    {"Small Room", "reverb-irs/small-room.wav", "reverb", "irreverb", "", "Reverb · impulse response"},
  };
  const auto fileBackedAssets = state.assets;
  state.assets.clear();
  appendDriveAssets(state);
  appendUtilityAssets(state);
  appendDaisyAssets(state);
  state.assets.push_back({"Split Left / Right", "", "amps", "dualRig", "split",
                          "Runs two independent chains in parallel"});
  state.assets.insert(state.assets.end(), fileBackedAssets.begin(), fileBackedAssets.end());
  return state;
}

void setActivePreset(UiState& state, std::size_t index, bool requestAudioSwap)
{
  if (index >= state.bank.presets.size()) {
    return;
  }
  state.activePreset = index;
  state.selectedBlock = 0;
  state.selectedBlockId.clear();
  if (requestAudioSwap) {
    state.pendingSlotRequest = static_cast<int>(index);
  }
  enterPresetMode(state);
  state.dirty = false;
  state.effectsBypassed = false;
  state.blockEditUndo.reset();
  state.pendingPreview.reset();
  state.navigationPrompt.reset();
  markUiChanged(state, UiChange::All);
}

void selectPreset(UiState& state, std::size_t index)
{
  setActivePreset(state, index, true);
}

void synchronizePresetSelection(UiState& state, std::size_t index)
{
  setActivePreset(state, index, false);
}

void enterPresetMode(UiState& state)
{
  state.mode = UiMode::Preset;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Navigation | UiChange::Drawers | UiChange::Parameters);
}

void enterEditMode(UiState& state)
{
  state.mode = UiMode::Edit;
  markUiChanged(state, UiChange::Navigation | UiChange::Header | UiChange::Chain);
}

void enterTunerMode(UiState& state)
{
  state.mode = UiMode::Tuner;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
  state.tuner = {};
  markUiChanged(state, UiChange::Navigation | UiChange::Drawers | UiChange::Telemetry);
}

void updateTunerTelemetry(UiState& state, UiTunerTelemetry telemetry)
{
  const bool changed = state.tuner.signalDetected != telemetry.signalDetected
    || state.tuner.note != telemetry.note
    || state.tuner.octave != telemetry.octave
    || std::fabs(state.tuner.frequencyHz - telemetry.frequencyHz) >= 0.05f
    || std::fabs(state.tuner.cents - telemetry.cents) >= 0.1f;
  state.tuner = std::move(telemetry);
  if (changed) {
    markUiChanged(state, UiChange::Telemetry);
  }
}

void openBlockDrawer(UiState& state)
{
  state.blockInsertIndex = state.bank.presets[state.activePreset].blocks.size();
  state.blockInsertRig.reset();
  state.blockInsertLane.reset();
  state.blockDrawerOpen = true;
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers | UiChange::Parameters);
}

void openBlockDrawerAt(UiState& state, std::size_t blockIndex)
{
  state.blockInsertIndex = std::min(blockIndex, state.bank.presets[state.activePreset].blocks.size());
  state.blockInsertRig.reset();
  state.blockInsertLane.reset();
  state.blockDrawerOpen = true;
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers | UiChange::Parameters);
}

void openLaneBlockDrawer(UiState& state, std::size_t rigIndex, std::size_t laneIndex,
                         std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (rigIndex >= blocks.size() || blocks[rigIndex].type != "dualRig"
      || laneIndex >= blocks[rigIndex].lanes.size()) {
    return;
  }
  state.blockInsertRig = rigIndex;
  state.blockInsertLane = laneIndex;
  state.blockInsertIndex = std::min(blockIndex, blocks[rigIndex].lanes[laneIndex].size());
  state.blockDrawerOpen = true;
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers | UiChange::Parameters);
}

void closeBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers);
}

UiBlock* selectedUiBlock(UiState& state)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) return nullptr;
  auto& topLevel = blocks[state.selectedBlock];
  if (state.selectedBlockId.empty() || state.selectedBlockId == topLevel.id) return &topLevel;
  for (auto& lane : topLevel.lanes) {
    const auto found = std::find_if(lane.begin(), lane.end(), [&](const UiBlock& block) {
      return block.id == state.selectedBlockId;
    });
    if (found != lane.end()) return &*found;
  }
  return &topLevel;
}

const UiBlock* selectedUiBlock(const UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) return nullptr;
  const auto& topLevel = blocks[state.selectedBlock];
  if (state.selectedBlockId.empty() || state.selectedBlockId == topLevel.id) return &topLevel;
  for (const auto& lane : topLevel.lanes) {
    const auto found = std::find_if(lane.begin(), lane.end(), [&](const UiBlock& block) {
      return block.id == state.selectedBlockId;
    });
    if (found != lane.end()) return &*found;
  }
  return &topLevel;
}

bool selectedBlockIsLaneChild(const UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) return false;
  const auto* selected = selectedUiBlock(state);
  return selected != nullptr && selected != &blocks[state.selectedBlock];
}

void selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  state.selectedBlock = blockIndex;
  state.selectedBlockId = blocks[blockIndex].id;
  state.paramTarget = UiParamTarget::Block;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = true;
  markUiChanged(state, UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void selectLaneBlock(UiState& state, std::size_t rigIndex, std::size_t laneIndex,
                     std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (rigIndex >= blocks.size() || blocks[rigIndex].type != "dualRig"
      || laneIndex >= blocks[rigIndex].lanes.size()
      || blockIndex >= blocks[rigIndex].lanes[laneIndex].size()) {
    return;
  }
  state.selectedBlock = rigIndex;
  state.selectedBlockId = blocks[rigIndex].lanes[laneIndex][blockIndex].id;
  state.paramTarget = UiParamTarget::Block;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = true;
  markUiChanged(state, UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void appendAssetBlock(UiState& state, std::size_t assetIndex)
{
  insertAssetBlock(state, assetIndex, state.bank.presets[state.activePreset].blocks.size());
}

void insertAssetBlock(UiState& state, std::size_t assetIndex, std::size_t blockIndex)
{
  if (!previewIsSynchronized(state)) return;
  if (assetIndex >= state.assets.size()) {
    return;
  }

  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blocks.size() >= kMaxEffectBlocks) {
    return;
  }

  const auto& asset = state.assets[assetIndex];
  const auto insertAt = std::min(blockIndex, blocks.size());
  if (asset.blockType == "dualRig") {
    const bool alreadySplit = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
      return block.enabled && (block.type == "dualRig" || block.type == "dualAmp");
    });
    if (alreadySplit) {
      setUiStatus(state, "Only one Split region is supported", true);
      return;
    }
    const bool standaloneAmp = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
      return block.enabled && (block.type == "nam" || block.type == "cab");
    });
    if (standaloneAmp) {
      setUiStatus(state, "Remove standalone NAM and IR blocks before adding Split", true);
      return;
    }
  }

  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  if (asset.blockType == "dualRig") {
    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(insertAt),
                  makeEmptyDualRig(blocks));
    state.bank.presets[state.activePreset].version = 2;
  } else {
    blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(insertAt),
                  blockFromAsset(state, asset, blocks));
  }
  state.selectedBlock = insertAt;
  state.selectedBlockId = blocks[insertAt].id;
  state.paramTarget = UiParamTarget::Block;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = true;
  state.dirty = true;
  queuePreview(state, previewRollback, "add " + asset.name);
  setUiStatus(state, asset.name + " added - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void insertLaneAssetBlock(UiState& state, std::size_t assetIndex, std::size_t rigIndex,
                          std::size_t laneIndex, std::size_t blockIndex)
{
  if (!previewIsSynchronized(state) || assetIndex >= state.assets.size()) return;
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (rigIndex >= blocks.size() || blocks[rigIndex].type != "dualRig"
      || laneIndex >= blocks[rigIndex].lanes.size()) {
    return;
  }
  const auto& asset = state.assets[assetIndex];
  if (asset.blockType == "dualRig" || asset.blockType == "dualAmp") {
    setUiStatus(state, "A Split cannot be placed inside another Split", true);
    return;
  }
  auto& lane = blocks[rigIndex].lanes[laneIndex];
  if (lane.size() >= kMaxEffectBlocks) {
    setUiStatus(state, "This lane is full", true);
    return;
  }

  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  const auto insertAt = std::min(blockIndex, lane.size());
  lane.insert(lane.begin() + static_cast<std::ptrdiff_t>(insertAt),
              blockFromAsset(state, asset, blocks));
  blocks[rigIndex].assetName = "Left " + std::to_string(blocks[rigIndex].lanes[0].size())
    + " blocks  /  Right " + std::to_string(blocks[rigIndex].lanes[1].size()) + " blocks";
  state.selectedBlock = rigIndex;
  state.selectedBlockId = blocks[rigIndex].id;
  state.paramTarget = UiParamTarget::Block;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
  state.dirty = true;
  queuePreview(state, previewRollback, "add " + asset.name + " to lane");
  setUiStatus(state, asset.name + " added to "
              + std::string(laneIndex == 0 ? "Left" : "Right") + " - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

bool moveLaneBlock(UiState& state, std::size_t rigIndex, std::size_t sourceLane,
                   std::size_t sourceIndex, std::size_t targetLane, std::size_t targetIndex)
{
  if (!previewIsSynchronized(state)) return false;
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (rigIndex >= blocks.size() || blocks[rigIndex].type != "dualRig"
      || sourceLane >= blocks[rigIndex].lanes.size()
      || targetLane >= blocks[rigIndex].lanes.size()) {
    return false;
  }
  auto& source = blocks[rigIndex].lanes[sourceLane];
  auto& target = blocks[rigIndex].lanes[targetLane];
  if (sourceIndex >= source.size()
      || (sourceLane != targetLane && target.size() >= kMaxEffectBlocks)) {
    return false;
  }

  targetIndex = std::min(targetIndex, target.size());
  if (sourceLane == targetLane && (targetIndex == sourceIndex || targetIndex == sourceIndex + 1)) {
    return false;
  }
  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  const auto movedName = source[sourceIndex].assetName;
  UiBlock moved = std::move(source[sourceIndex]);
  source.erase(source.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
  if (sourceLane == targetLane && targetIndex > sourceIndex) --targetIndex;
  target.insert(target.begin() + static_cast<std::ptrdiff_t>(std::min(targetIndex, target.size())),
                std::move(moved));
  blocks[rigIndex].assetName = "Left " + std::to_string(blocks[rigIndex].lanes[0].size())
    + " blocks  /  Right " + std::to_string(blocks[rigIndex].lanes[1].size()) + " blocks";
  state.selectedBlock = rigIndex;
  state.selectedBlockId = blocks[rigIndex].id;
  state.paramTarget = UiParamTarget::Block;
  state.paramDrawerOpen = false;
  state.blockDrawerOpen = false;
  state.dirty = true;
  queuePreview(state, previewRollback, "move " + movedName + " between lanes");
  setUiStatus(state, movedName + " moved to "
              + std::string(targetLane == 0 ? "Left" : "Right") + " - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
  return true;
}

void moveBlock(UiState& state, std::size_t from, std::size_t to)
{
  if (!previewIsSynchronized(state)) return;
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (from >= blocks.size() || to >= blocks.size() || from == to) {
    return;
  }

  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  const std::string movedName = blocks[from].assetName;
  auto block = std::move(blocks[from]);
  blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(from));
  blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(to), std::move(block));
  state.selectedBlock = to;
  state.selectedBlockId = blocks[to].id;
  state.dirty = true;
  queuePreview(state, previewRollback, "move " + movedName);
  setUiStatus(state, movedName + " moved - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters);
}

bool deleteSelectedBlock(UiState& state)
{
  if (!previewIsSynchronized(state)) return false;
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return false;
  }

  if (selectedBlockIsLaneChild(state)) {
    auto& rig = blocks[state.selectedBlock];
    for (std::size_t laneIndex = 0; laneIndex < rig.lanes.size(); ++laneIndex) {
      auto& lane = rig.lanes[laneIndex];
      const auto found = std::find_if(lane.begin(), lane.end(), [&](const UiBlock& block) {
        return block.id == state.selectedBlockId;
      });
      if (found == lane.end()) continue;
      const auto previewRollback = previewSnapshot(state);
      rememberBlockEdit(state);
      const std::string deletedName = found->assetName;
      const std::string deletedId = found->id;
      lane.erase(found);
      auto& assignment = state.bank.presets[state.activePreset].expression;
      if (assignment && assignment->blockId == deletedId) assignment.reset();
      auto& bindings = state.bank.presets[state.activePreset].midiBindings;
      for (auto& binding : bindings) {
        std::erase_if(binding.actions, [&](const PresetMidiAction& action) {
          return action.blockId == deletedId;
        });
      }
      std::erase_if(bindings, [](const PresetMidiBinding& binding) {
        return binding.actions.empty();
      });
      rig.assetName = "Left " + std::to_string(rig.lanes[0].size())
        + " blocks  /  Right " + std::to_string(rig.lanes[1].size()) + " blocks";
      state.selectedBlockId = rig.id;
      state.dirty = true;
      state.paramDrawerOpen = false;
      setUiStatus(state, deletedName + " deleted from "
                  + std::string(laneIndex == 0 ? "Left" : "Right") + " - Undo");
      queuePreview(state, previewRollback, "delete " + deletedName + " from lane");
      markUiChanged(state, UiChange::Header | UiChange::Chain
                           | UiChange::Parameters | UiChange::Drawers);
      return true;
    }
    return false;
  }

  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  const std::string deletedName = blocks[state.selectedBlock].assetName;
  const auto containsAssignedBlock = [&](const auto& self, const UiBlock& block,
                                         const std::string& id) -> bool {
    if (block.id == id) return true;
    for (const auto& lane : block.lanes) {
      for (const auto& child : lane) if (self(self, child, id)) return true;
    }
    return false;
  };
  auto& assignment = state.bank.presets[state.activePreset].expression;
  if (assignment && containsAssignedBlock(containsAssignedBlock,
                                           blocks[state.selectedBlock], assignment->blockId)) {
    assignment.reset();
  }
  auto& midiBindings = state.bank.presets[state.activePreset].midiBindings;
  for (auto& binding : midiBindings) {
    std::erase_if(binding.actions, [&](const PresetMidiAction& action) {
      return containsAssignedBlock(containsAssignedBlock,
                                   blocks[state.selectedBlock], action.blockId);
    });
  }
  std::erase_if(midiBindings, [](const PresetMidiBinding& binding) {
    return binding.actions.empty();
  });
  blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(state.selectedBlock));
  if (blocks.empty()) {
    state.selectedBlock = 0;
    state.selectedBlockId.clear();
    state.paramDrawerOpen = false;
  } else {
    state.selectedBlock = std::min(state.selectedBlock, blocks.size() - 1);
    state.selectedBlockId = blocks[state.selectedBlock].id;
    state.paramTarget = UiParamTarget::Block;
  }
  state.dirty = true;
  state.paramDrawerOpen = false;
  setUiStatus(state, deletedName + " deleted - Undo");
  queuePreview(state, previewRollback, "delete " + deletedName);
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
  return true;
}

bool undoLastBlockEdit(UiState& state)
{
  if (!previewIsSynchronized(state) || !state.blockEditUndo.has_value()) {
    return false;
  }
  const auto rollback = previewSnapshot(state);
  auto snapshot = std::move(*state.blockEditUndo);
  state.blockEditUndo.reset();
  state.bank.presets[state.activePreset].blocks = std::move(snapshot.blocks);
  state.bank.presets[state.activePreset].expression = std::move(snapshot.expression);
  state.bank.presets[state.activePreset].midiBindings = std::move(snapshot.midiBindings);
  state.selectedBlock = snapshot.selectedBlock;
  state.selectedBlockId = std::move(snapshot.selectedBlockId);
  state.paramTarget = snapshot.paramTarget;
  state.dirty = snapshot.dirty;
  state.blockDrawerOpen = snapshot.blockDrawerOpen;
  state.paramDrawerOpen = snapshot.paramDrawerOpen;
  setUiStatus(state, "Change undone");
  queuePreview(state, rollback, "undo change");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
  return true;
}

void closeParamDrawer(UiState& state)
{
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Parameters | UiChange::Drawers);
}

void setCategoryFilter(UiState& state, std::string filter)
{
  // Keep this in step with the drawer's filter buttons. A category missing here
  // falls back to "all", which reads as a dead button.
  static constexpr std::array valid = {
    "all", "amps", "cabs", "drive", "utility", "modulation", "delay", "reverb",
  };
  const auto found = std::find(valid.begin(), valid.end(), filter);
  state.categoryFilter = found == valid.end() ? "all" : std::move(filter);
  markUiChanged(state, UiChange::Drawers);
}

Preset activePresetToPreset(const UiState& state)
{
  Preset preset;
  const auto& uiPreset = state.bank.presets[state.activePreset];
  preset.version = uiPreset.version;
  preset.name = uiPreset.name;
  preset.routing = "serial";
  preset.global = uiPreset.global;
  preset.expression = uiPreset.expression;
  preset.midiBindings = uiPreset.midiBindings;
  const auto convertBlock = [&](const auto& self, const UiBlock& block) -> PresetBlock {
    PresetBlock converted{block.id, block.type, block.enabled, block.assetPath,
                          block.params.is_null() ? nlohmann::json::object() : block.params};
    for (std::size_t lane = 0; lane < converted.lanes.size(); ++lane) {
      for (const auto& child : block.lanes[lane]) converted.lanes[lane].push_back(self(self, child));
    }
    return converted;
  };
  for (const auto& block : uiPreset.blocks) preset.blocks.push_back(convertBlock(convertBlock, block));
  return preset;
}

bool presetHasUnavailableAssets(const UiState& state, std::size_t presetIndex)
{
  if (presetIndex >= state.bank.presets.size()) return false;
  const auto unavailable = [&](const auto& self, const UiBlock& block) -> bool {
    if (!block.enabled) return false;
    if (block.type == "dualRig") {
      for (const auto& lane : block.lanes) {
        for (const auto& child : lane) {
          if (self(self, child)) return true;
        }
      }
      return false;
    }
    if (block.enabled && block.type == "dualAmp") {
      for (const char* key : {"leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"}) {
        const auto path = block.params.value(key, std::string{});
        if (path.empty()) return true;
        const bool installed = std::any_of(state.assets.begin(), state.assets.end(),
          [&](const UiAsset& asset) { return asset.path == path; });
        if (!installed) return true;
      }
      return false;
    }
    if (block.type != "nam" && block.type != "cab") return false;
    if (block.assetPath.empty()) return true;
    const bool installed = std::any_of(state.assets.begin(), state.assets.end(), [&](const UiAsset& asset) {
      return asset.path == block.assetPath;
    });
    return !installed;
  };
  for (const auto& block : state.bank.presets[presetIndex].blocks) {
    if (unavailable(unavailable, block)) return true;
  }
  return false;
}

void replaceActivePreset(UiState& state, const Preset& preset)
{
  auto& uiPreset = state.bank.presets[state.activePreset];
  uiPreset.version = preset.version;
  uiPreset.name = preset.name;
  uiPreset.global = preset.global;
  uiPreset.expression = preset.expression;
  uiPreset.midiBindings = preset.midiBindings;
  uiPreset.blocks.clear();
  for (const auto& block : preset.blocks) {
    if (uiPreset.blocks.size() == kMaxEffectBlocks) {
      break;
    }
    const auto convertBlock = [&](const auto& self, const PresetBlock& source) -> UiBlock {
      UiBlock converted{source.id,
                        source.type,
                        labelForBlockType(source.type),
                        assetNameForBlock(state, source),
                        source.asset,
                        source.enabled,
                        paramsWithKnownDefaults(source.type, source.params)};
      for (std::size_t lane = 0; lane < converted.lanes.size(); ++lane) {
        for (const auto& child : source.lanes[lane]) converted.lanes[lane].push_back(self(self, child));
      }
      return converted;
    };
    uiPreset.blocks.push_back(convertBlock(convertBlock, block));
  }
  state.selectedBlock = 0;
  state.selectedBlockId.clear();
  state.dirty = false;
  state.pendingPreview.reset();
  state.paramDrawerOpen = false;
  state.blockDrawerOpen = false;
  state.blockEditUndo.reset();
  state.navigationPrompt.reset();
  markUiChanged(state, UiChange::All);
}

void selectGlobalParams(UiState& state)
{
  state.paramTarget = UiParamTarget::Globals;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = true;
  markUiChanged(state, UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void setSelectedBlockEnabled(UiState& state, bool enabled)
{
  if (!previewIsSynchronized(state)) return;
  auto* block = selectedUiBlock(state);
  if (!block || block->enabled == enabled) return;
  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  block->enabled = enabled;
  state.dirty = true;
  queuePreview(state, previewRollback, std::string(enabled ? "enable " : "bypass ")
                                      + block->assetName);
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters);
}

bool setSelectedBlockEnabledLive(UiState& state, bool enabled)
{
  if (!previewIsSynchronized(state)) return false;
  auto* block = selectedUiBlock(state);
  if (!block || block->enabled == enabled) return false;
  rememberBlockEdit(state);
  block->enabled = enabled;
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters);
  return true;
}

void setActiveInputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.inputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

void setActiveOutputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.outputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

void setMasterVolume(UiState& state, int volume)
{
  volume = std::clamp(volume, 0, 100);
  if (state.masterVolume == volume) {
    return;
  }
  state.masterVolume = volume;
  markUiChanged(state, UiChange::Header);
}

void setSelectedBlockParam(UiState& state, const std::string& key, float value)
{
  auto* selected = selectedUiBlock(state);
  if (!selected) return;
  auto& block = *selected;
  if (block.type == "cab") {
    if (key == "levelDb") {
      value = clampFloat(value, -60.0f, 12.0f);
    } else if (key == "mix") {
      value = clampFloat(value, 0.0f, 1.0f);
    }
  } else if (block.type == "dualAmp" || block.type == "dualRig") {
    if (!previewIsSynchronized(state)) return;
    if (key == "leftCabLevelDb" || key == "rightCabLevelDb"
        || key == "leftLevelDb" || key == "rightLevelDb") {
      value = clampFloat(value, -60.0f, 12.0f);
    } else if (key == "leftCabMix" || key == "rightCabMix") {
      value = clampFloat(value, 0.0f, 1.0f);
    } else {
      return;
    }
  } else if (const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""))) {
    for (const auto& param : descriptor->params) {
      if (param.key == key) {
        value = clampFloat(value, 0.0f, 1.0f);
        break;
      }
    }
  } else if (block.type == "dynamics" && block.params.value("mode", "") == "compressor") {
    if (key == "threshold_db") value = clampFloat(value, -60.0f, 0.0f);
    else if (key == "ratio") value = clampFloat(value, 1.0f, 20.0f);
    else if (key == "attack_ms") value = clampFloat(value, 0.1f, 200.0f);
    else if (key == "release_ms") value = clampFloat(value, 10.0f, 2000.0f);
    else if (key == "knee_db" || key == "makeup_db") value = clampFloat(value, 0.0f, 24.0f);
    else if (key == "input_gain_db") value = clampFloat(value, -24.0f, 24.0f);
    else if (key == "mix") value = clampFloat(value, 0.0f, 1.0f);
    else if (key == "sidechain_hpf_hz") value = clampFloat(value, 20.0f, 500.0f);
  } else if (block.type == "dynamics" && block.params.value("mode", "") == "noise_gate") {
    if (key == "threshold_db") value = clampFloat(value, -80.0f, 0.0f);
    else if (key == "reduction_db") value = clampFloat(value, 0.0f, 96.0f);
    else if (key == "attack_ms") value = clampFloat(value, 0.1f, 50.0f);
    else if (key == "hold_ms") value = clampFloat(value, 0.0f, 500.0f);
    else if (key == "release_ms") value = clampFloat(value, 10.0f, 2000.0f);
    else if (key == "hysteresis_db") value = clampFloat(value, 0.0f, 18.0f);
    else if (key == "sidechain_hpf_hz") value = clampFloat(value, 20.0f, 500.0f);
  } else if (block.type == "wah") {
    if (key == "position") value = clampFloat(value, 0.0f, 1.0f);
    else if (key == "level") value = clampFloat(value, -24.0f, 24.0f);
  }
  const auto existing = block.params.find(key);
  if ((block.type == "dualAmp" || block.type == "dualRig")
      && existing != block.params.end() && existing->is_number()
      && existing->get<float>() == value) return;
  const auto previewRollback = (block.type == "dualAmp" || block.type == "dualRig")
    ? std::optional<UiPreviewSnapshot>{previewSnapshot(state)} : std::nullopt;
  if (block.type == "dualAmp" || block.type == "dualRig") rememberBlockEdit(state);
  block.params[key] = value;
  state.dirty = true;
  if (previewRollback) {
    queuePreview(state, *previewRollback, "update " + block.assetName);
  }
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

void setSelectedBlockParamValue(UiState& state, const std::string& key, nlohmann::json value)
{
  if (!previewIsSynchronized(state)) return;
  auto* selected = selectedUiBlock(state);
  if (!selected) return;
  auto& block = *selected;
  const bool compressorValue = block.type == "dynamics"
    && block.params.value("mode", "") == "compressor"
    && (key == "detector" || key == "auto_makeup");
  // Tape speed is a load-time property: it moves filter and solver
  // coefficients, so it reaches the engine by rebuilding the chain the way the
  // compressor's detector does. This allow-list is the only route a choice edit
  // has to requeue a preview.
  const bool tapeSpeedValue = block.type == "distortion"
    && block.params.value("mode", "") == "tape"
    && key == "speed" && value.is_string()
    && (value == "15" || value == "30");
  const bool namNanoValue = block.type == "nam" && key == "useNano" && value.is_boolean();
  const bool namInputValue = block.type == "nam" && key == "inputMode" && value.is_string()
    && (value == "sum" || value == "left" || value == "right");
  const bool dualAmpInputValue = block.type == "dualAmp" && key == "inputMode" && value.is_string()
    && (value == "sum" || value == "left" || value == "right");
  const bool dualAmpToggleValue = block.type == "dualAmp" && value.is_boolean()
    && (key == "leftUseNano" || key == "rightUseNano"
        || key == "leftPolarityInvert" || key == "rightPolarityInvert");
  const bool dualRigInputValue = block.type == "dualRig" && key == "inputMode" && value.is_string()
    && (value == "sum" || value == "left" || value == "right");
  const bool dualRigToggleValue = block.type == "dualRig" && value.is_boolean()
    && (key == "leftPolarityInvert" || key == "rightPolarityInvert");
  if (!compressorValue && !tapeSpeedValue && !namNanoValue && !namInputValue
      && !dualAmpInputValue && !dualAmpToggleValue
      && !dualRigInputValue && !dualRigToggleValue) {
    return;
  }
  if (block.params.value(key, nlohmann::json{}) == value) return;
  const auto previewRollback = previewSnapshot(state);
  rememberBlockEdit(state);
  block.params[key] = std::move(value);
  state.dirty = true;
  queuePreview(state, previewRollback, "update " + block.assetName);
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

ParametricEqParams selectedParametricEqParams(const UiState& state)
{
  const auto* selected = selectedUiBlock(state);
  if (!selected) return defaultParametricEqParams();
  const auto& block = *selected;
  if (block.type != "eq" || !isParametricEqMode(block.params)) {
    return defaultParametricEqParams();
  }
  return parametricEqParamsFromJson(block.params);
}

bool setSelectedEqBand(UiState& state, std::size_t bandIndex, EqBandParams params)
{
  auto* selected = selectedUiBlock(state);
  if (!selected || bandIndex >= kParametricEqBandCount) return false;
  auto& block = *selected;
  if (block.type != "eq" || !isParametricEqMode(block.params)) {
    return false;
  }

  auto eqParams = parametricEqParamsFromJson(block.params);
  auto normalization = defaultParametricEqParams();
  normalization.bands[0] = params;
  params = parametricEqParamsFromJson(parametricEqParamsToJson(normalization)).bands[0];
  if (eqParams.bands[bandIndex] == params) {
    return true;
  }
  eqParams.bands[bandIndex] = params;
  block.params = parametricEqParamsToJson(eqParams);
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
  return true;
}

bool resetSelectedEqBand(UiState& state, std::size_t bandIndex)
{
  if (bandIndex >= kParametricEqBandCount) {
    return false;
  }
  return setSelectedEqBand(state, bandIndex, defaultParametricEqBand(bandIndex));
}

bool setSelectedEqPassFilter(UiState& state, EqPassFilterKind kind, EqPassFilterParams params)
{
  auto* selected = selectedUiBlock(state);
  if (!selected) return false;
  auto& block = *selected;
  if (block.type != "eq" || !isParametricEqMode(block.params)) {
    return false;
  }

  auto eqParams = parametricEqParamsFromJson(block.params);
  auto normalization = defaultParametricEqParams();
  if (kind == EqPassFilterKind::HighPass) {
    normalization.highPass = params;
    params = parametricEqParamsFromJson(parametricEqParamsToJson(normalization)).highPass;
    if (eqParams.highPass == params) return true;
    eqParams.highPass = params;
  } else {
    normalization.lowPass = params;
    params = parametricEqParamsFromJson(parametricEqParamsToJson(normalization)).lowPass;
    if (eqParams.lowPass == params) return true;
    eqParams.lowPass = params;
  }
  block.params = parametricEqParamsToJson(eqParams);
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
  return true;
}

bool resetSelectedEqPassFilter(UiState& state, EqPassFilterKind kind)
{
  return setSelectedEqPassFilter(state, kind, defaultEqPassFilter(kind));
}

bool previewIsSynchronized(const UiState& state)
{
  return !state.pendingPreview.has_value();
}

UiPreviewSnapshot captureUiPreviewSnapshot(const UiState& state)
{
  return previewSnapshot(state);
}

bool queuePreview(UiState& state, UiPreviewSnapshot rollback, std::string operation)
{
  if (!previewIsSynchronized(state)) return false;
  state.pendingPreview = UiPreviewTransaction{std::move(rollback), std::move(operation)};
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
  return true;
}

const UiPreviewTransaction* pendingStructuralPreview(const UiState& state)
{
  return state.pendingPreview ? &*state.pendingPreview : nullptr;
}

void completeStructuralPreview(UiState& state)
{
  state.pendingPreview.reset();
  setUiStatus(state, "Chain updated");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void failStructuralPreview(UiState& state, std::string error)
{
  if (state.pendingPreview.has_value()) {
    const auto& rollback = state.pendingPreview->rollback;
    state.bank.presets[state.activePreset] = rollback.preset;
    state.selectedBlock = rollback.selectedBlock;
    state.selectedBlockId = rollback.selectedBlockId;
    state.paramTarget = rollback.paramTarget;
    state.dirty = rollback.dirty;
    state.blockDrawerOpen = rollback.blockDrawerOpen;
    state.paramDrawerOpen = rollback.paramDrawerOpen;
    state.blockEditUndo = rollback.blockEditUndo;
  }
  state.pendingPreview.reset();
  setUiStatus(state, "Could not apply chain: " + std::move(error), true);
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

bool requestPresetNavigation(UiState& state, UiNavigationTarget target)
{
  if (!previewIsSynchronized(state) || target.bank < 0 || target.bank > 99
      || target.preset >= state.bank.presets.size()) {
    return false;
  }
  if (target.bank == state.activeBank && target.preset == state.activePreset) return false;
  if (state.dirty) {
    state.navigationPrompt = target;
    markUiChanged(state, UiChange::Drawers | UiChange::Status);
    return false;
  }
  return true;
}

std::optional<UiNavigationTarget> confirmNavigation(UiState& state, UiNavigationDecision decision)
{
  if (!state.navigationPrompt.has_value()) return std::nullopt;
  if (decision == UiNavigationDecision::Cancel) {
    state.navigationPrompt.reset();
    markUiChanged(state, UiChange::Drawers | UiChange::Status);
    return std::nullopt;
  }
  auto target = std::move(state.navigationPrompt);
  state.navigationPrompt.reset();
  markUiChanged(state, UiChange::Drawers | UiChange::Status);
  return target;
}

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry)
{
  const bool visibleChanged = state.telemetry.overBudget != telemetry.overBudget
    || state.telemetry.maxMs != telemetry.maxMs
    || state.telemetry.bufferFreePercent != telemetry.bufferFreePercent
    || state.effectsBypassed != telemetry.bypassed;
  state.telemetry = telemetry;
  state.effectsBypassed = telemetry.bypassed;
  if (visibleChanged) markUiChanged(state, UiChange::Telemetry);
}

void updateClipDebugTelemetry(UiState& state, UiClipDebugTelemetry telemetry)
{
  const bool visibleChanged = state.clipDebug.enabled != telemetry.enabled
    || state.clipDebug.overloaded != telemetry.overloaded
    || state.clipDebug.firstStage != telemetry.firstStage
    || state.clipDebug.peakDb != telemetry.peakDb
    || state.clipDebug.overloadFrames != telemetry.overloadFrames
    || state.clipDebug.limiterFrames != telemetry.limiterFrames;
  state.clipDebug = std::move(telemetry);
  if (visibleChanged) markUiChanged(state, UiChange::Telemetry);
}

void updateCompressorGainReduction(UiState& state, float reductionDb)
{
  state.compressorGainReductionDb = reductionDb;
}

void updateControlInputTelemetry(UiState& state, UiControlInputTelemetry telemetry)
{
  telemetry.expressionPosition = std::clamp(telemetry.expressionPosition, 0.0f, 1.0f);
  const bool visibleChanged = state.controlInputs.midiConfigured != telemetry.midiConfigured
    || state.controlInputs.midiConnected != telemetry.midiConnected
    || state.controlInputs.expressionConnected != telemetry.expressionConnected
    || state.controlInputs.expressionPositionKnown != telemetry.expressionPositionKnown
    || std::fabs(state.controlInputs.expressionPosition - telemetry.expressionPosition) >= 0.005f
    || state.controlInputs.expressionRawKnown != telemetry.expressionRawKnown
    || state.controlInputs.expressionRaw != telemetry.expressionRaw;
  state.controlInputs = telemetry;
  if (visibleChanged) markUiChanged(state, UiChange::Telemetry);
}

bool parameterSupportsExpression(const UiState& state, const ParameterControl& control)
{
  if (state.paramTarget != UiParamTarget::Block
      || control.kind != ParameterControlKind::Continuous
      || !(control.maximum > control.minimum)) return false;
  const auto* block = selectedUiBlock(state);
  if (!block) return false;
  if (block->type == "mod" || block->type == "delay" || block->type == "reverb"
      || block->type == "dynamics" || block->type == "wah") return true;
  return block->type == "cab" && (control.key == "mix" || control.key == "levelDb");
}

bool parameterHasMidiBinding(const UiState& state, const ParameterControl& control)
{
  if (state.paramTarget != UiParamTarget::Block) return false;
  const auto* block = selectedUiBlock(state);
  if (!block) return false;
  const auto& bindings = state.bank.presets[state.activePreset].midiBindings;
  return std::any_of(bindings.begin(), bindings.end(), [&](const PresetMidiBinding& binding) {
    return std::any_of(binding.actions.begin(), binding.actions.end(),
      [&](const PresetMidiAction& action) {
        return action.target == PresetMidiTargetType::Parameter
          && action.blockId == block->id && action.parameter == control.key;
      });
  });
}

bool toggleExpressionAssignment(UiState& state, const ParameterControl& control)
{
  if (!parameterSupportsExpression(state, control)) return false;
  const auto* block = selectedUiBlock(state);
  if (!block) return false;
  auto& assignment = state.bank.presets[state.activePreset].expression;
  if (assignment && assignment->blockId == block->id
      && assignment->parameter == control.key) {
    assignment.reset();
    setUiStatus(state, "Expression assignment cleared");
  } else {
    assignment = PresetExpression{block->id, control.key,
                                  control.minimum, control.maximum, false};
    setUiStatus(state, "Expression assigned to " + block->label + " / " + control.label);
  }
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters | UiChange::Telemetry);
  return true;
}

bool beginMidiLearn(UiState& state, const ParameterControl& control)
{
  if (!parameterSupportsExpression(state, control) || selectedBlockIsLaneChild(state)) return false;
  const auto* block = selectedUiBlock(state);
  if (!block) return false;
  state.midiLearn = {};
  state.midiLearn.stage = UiMidiLearnStage::Waiting;
  state.midiLearn.action = {
    PresetMidiTargetType::Parameter, block->id, control.key,
    control.minimum, control.maximum,
  };
  state.midiLearn.targetMinimum = control.minimum;
  state.midiLearn.targetMaximum = control.maximum;
  state.midiLearn.targetCurrent = control.value;
  setUiStatus(state, "MIDI Learn: move a pedal or press a footswitch");
  markUiChanged(state, UiChange::Parameters | UiChange::Status);
  return true;
}

bool beginMidiLearnForBlockEnabled(UiState& state)
{
  if (state.paramTarget != UiParamTarget::Block || selectedBlockIsLaneChild(state)) return false;
  const auto* block = selectedUiBlock(state);
  if (!block) return false;
  state.midiLearn = {};
  state.midiLearn.stage = UiMidiLearnStage::Waiting;
  state.midiLearn.mode = PresetMidiBindingMode::Toggle;
  state.midiLearn.modeExplicit = true;
  state.midiLearn.action = {
    PresetMidiTargetType::BlockEnabled, block->id, "",
    block->enabled ? 1.0f : 0.0f, block->enabled ? 0.0f : 1.0f,
  };
  state.midiLearn.targetMinimum = 0.0f;
  state.midiLearn.targetMaximum = 1.0f;
  state.midiLearn.targetCurrent = block->enabled ? 1.0f : 0.0f;
  setUiStatus(state, "MIDI Learn: press a footswitch for bypass");
  markUiChanged(state, UiChange::Parameters | UiChange::Status);
  return true;
}

bool observeMidiLearnControlChange(
  UiState& state, int channel, int controlChange, int value)
{
  if (state.midiLearn.stage == UiMidiLearnStage::None) return false;
  channel = std::clamp(channel, 0, 15);
  controlChange = std::clamp(controlChange, 0, 127);
  value = std::clamp(value, 0, 127);
  auto& learn = state.midiLearn;
  if (learn.stage == UiMidiLearnStage::Waiting) {
    learn.channel = channel;
    learn.controlChange = controlChange;
    learn.stage = UiMidiLearnStage::Captured;
  }
  if (controlChange == learn.controlChange && channel == learn.channel
      && learn.stage != UiMidiLearnStage::Advanced) {
    learn.observedMinimum = std::min(learn.observedMinimum, value);
    learn.observedMaximum = std::max(learn.observedMaximum, value);
    ++learn.observedCount;
    markUiChanged(state, UiChange::Status | UiChange::Parameters);
  }
  // While the learn sheet is open, MIDI belongs to it and must not also
  // change a bank, tuner state, or an older mapping.
  return true;
}

void showAdvancedMidiLearn(UiState& state)
{
  if (state.midiLearn.stage != UiMidiLearnStage::Captured) return;
  state.midiLearn.stage = UiMidiLearnStage::Advanced;
  markUiChanged(state, UiChange::Parameters | UiChange::Status);
}

void setMidiLearnMode(UiState& state, PresetMidiBindingMode mode)
{
  auto& learn = state.midiLearn;
  if (learn.stage == UiMidiLearnStage::None || learn.controlChange < 0) return;
  learn.mode = mode;
  learn.modeExplicit = true;
  if (mode == PresetMidiBindingMode::Continuous) {
    learn.action.value1 = learn.targetMinimum;
    learn.action.value2 = learn.targetMaximum;
  } else {
    learn.action.value1 = learn.targetCurrent;
    const float distanceToMinimum = std::fabs(learn.targetCurrent - learn.targetMinimum);
    const float distanceToMaximum = std::fabs(learn.targetMaximum - learn.targetCurrent);
    learn.action.value2 = distanceToMaximum >= distanceToMinimum
      ? learn.targetMaximum : learn.targetMinimum;
  }
  markUiChanged(state, UiChange::Parameters);
}

void setMidiLearnEndpoint(UiState& state, std::size_t endpoint, float value)
{
  auto& learn = state.midiLearn;
  if (learn.stage != UiMidiLearnStage::Advanced || !std::isfinite(value)) return;
  value = std::clamp(value, learn.targetMinimum, learn.targetMaximum);
  if (endpoint == 0) learn.action.value1 = value;
  else if (endpoint == 1) learn.action.value2 = value;
  else return;
  markUiChanged(state, UiChange::Parameters);
}

bool commitMidiLearn(UiState& state)
{
  auto& learn = state.midiLearn;
  if ((learn.stage != UiMidiLearnStage::Captured
       && learn.stage != UiMidiLearnStage::Advanced)
      || learn.controlChange < 0) return false;
  const auto previewRollback = previewSnapshot(state);
  const auto* targetBlock = selectedUiBlock(state);
  const bool mustPrepareBypassedBlock = previewIsSynchronized(state)
    && learn.action.target == PresetMidiTargetType::BlockEnabled
    && targetBlock && targetBlock->id == learn.action.blockId && !targetBlock->enabled;
  if (!learn.modeExplicit) {
    const bool swept = learn.observedCount >= 3
      && learn.observedMaximum - learn.observedMinimum >= 8;
    learn.mode = swept ? PresetMidiBindingMode::Continuous : PresetMidiBindingMode::Toggle;
    if (!swept) setMidiLearnMode(state, PresetMidiBindingMode::Toggle);
  }

  auto& bindings = state.bank.presets[state.activePreset].midiBindings;
  const auto sameTarget = [&](const PresetMidiAction& action) {
    return action.target == learn.action.target
      && action.blockId == learn.action.blockId
      && action.parameter == learn.action.parameter;
  };
  for (auto& binding : bindings) {
    std::erase_if(binding.actions, sameTarget);
  }
  std::erase_if(bindings, [](const PresetMidiBinding& binding) {
    return binding.actions.empty();
  });

  auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const PresetMidiBinding& item) {
    return item.channel == learn.channel
      && item.controlChange == learn.controlChange;
  });
  if (binding == bindings.end()) {
    bindings.push_back({learn.channel, static_cast<std::uint8_t>(learn.controlChange),
                        learn.mode, {learn.action}});
  } else {
    binding->mode = learn.mode;
    binding->actions.push_back(learn.action);
  }
  const auto controller = learn.controlChange;
  state.midiLearn = {};
  state.dirty = true;
  if (mustPrepareBypassedBlock) {
    queuePreview(state, previewRollback, "prepare MIDI scene");
  }
  setUiStatus(state, "MIDI CC " + std::to_string(controller) + " learned");
  markUiChanged(state, UiChange::Header | UiChange::Parameters | UiChange::Status);
  return true;
}

void cancelMidiLearn(UiState& state)
{
  if (state.midiLearn.stage == UiMidiLearnStage::None) return;
  state.midiLearn = {};
  setUiStatus(state, "MIDI Learn cancelled");
  markUiChanged(state, UiChange::Parameters | UiChange::Status);
}

void setUiStatus(UiState& state, std::string message, bool isError)
{
  state.statusMessage = std::move(message);
  state.statusIsError = isError;
  markUiChanged(state, UiChange::Status);
}

int consumePendingSlotRequest(UiState& state)
{
  const int slot = state.pendingSlotRequest;
  state.pendingSlotRequest = -1;
  return slot;
}

void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot)
{
  state.assets.clear();
  appendDriveAssets(state);
  appendUtilityAssets(state);
  appendDaisyAssets(state);
  state.assets.push_back({"Split Left / Right", "", "amps", "dualRig", "split",
                          "Runs two independent chains in parallel"});
  appendAssetsFrom(state, dataRoot / "models", ".nam", "amps", "Amp · neural capture");
  appendAssetsFrom(state, dataRoot / "irs", ".wav", "cabs", "Cab · impulse response");
  appendAssetsFrom(state, dataRoot / "reverb-irs", ".wav", "reverb",
                   "Reverb · impulse response", "irreverb");
  markUiChanged(state, UiChange::Assets | UiChange::Drawers);
}

bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error)
{
  try {
    state.activePreset = static_cast<std::size_t>(slot.preset);
    replaceActivePreset(state, store.load(slot));
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

void loadBankFromStore(UiState& state, const PresetStore& store, int bank)
{
  bank = std::clamp(bank, 0, 99);
  state.activeBank = bank;
  state.bank.name = bankName(bank);
  const auto previous = state.activePreset;
  for (std::size_t i = 0; i < state.bank.presets.size(); ++i) {
    std::string error;
    if (!loadPresetSlotFromStore(state, store, {bank, static_cast<int>(i)}, error)) {
      state.bank.presets[i] = emptyPreset(i);
    }
  }
  state.activePreset = std::min(previous, state.bank.presets.size() - 1);
  state.selectedBlock = 0;
  state.selectedBlockId.clear();
  state.dirty = false;
  state.paramDrawerOpen = false;
  state.blockDrawerOpen = false;
  state.blockEditUndo.reset();
  state.pendingPreview.reset();
  state.navigationPrompt.reset();
  markUiChanged(state, UiChange::All);
}

bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error)
{
  try {
    store.save({bank, static_cast<int>(state.activePreset)}, activePresetToPreset(state));
    state.dirty = false;
    state.blockEditUndo.reset();
    markUiChanged(state, UiChange::Header | UiChange::Status);
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

} // namespace ardor
