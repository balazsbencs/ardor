#include "ui/UiModel.h"

#include "daisyfx/DaisyFxCatalog.h"

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
  if (const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""))) {
    return descriptor->name;
  }
  if (block.type == "dynamics" && block.params.value("mode", "") == "compressor") {
    return "Compressor";
  }
  if (block.type == "eq" && isParametricEqMode(block.params)) {
    return "Five Band EQ";
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

nlohmann::json paramsWithKnownDefaults(const std::string& type, const nlohmann::json& supplied)
{
  nlohmann::json params = supplied.is_object() ? supplied : nlohmann::json::object();
  nlohmann::json defaults = nlohmann::json::object();
  if (const auto* descriptor = findDaisyFxDescriptor(type, params.value("mode", ""))) {
    defaults = defaultDaisyFxParams(*descriptor);
  } else if (type == "nam" && !params.contains("quality")) {
    defaults = {{"useNano", false}};
  } else if (type == "dynamics" && params.value("mode", "") == "compressor") {
    defaults = defaultCompressorParams();
  } else if (type == "eq" && isParametricEqMode(params)) {
    return parametricEqParamsToJson(parametricEqParamsFromJson(params));
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
  for (const auto& block : blocks) {
    if (block.id.rfind("block-", 0) != 0) {
      continue;
    }
    try {
      maxId = std::max(maxId, std::stoi(block.id.substr(6)));
    } catch (const std::exception&) {
    }
  }
  return "block-" + std::to_string(maxId + 1);
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
    state.selectedBlock,
    state.paramTarget,
    state.dirty,
    state.requiresEngineReload,
    state.blockDrawerOpen,
    state.paramDrawerOpen,
  };
}

void appendAssetsFrom(UiState& state, const std::filesystem::path& dir, const std::string& ext, const std::string& type)
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
    state.assets.push_back({entry.path().stem().string(), relative, type});
  }
}

void appendDaisyAssets(UiState& state)
{
  for (const auto& descriptor : daisyFxCatalog()) {
    state.assets.push_back({descriptor.name, "", categoryForDaisyKind(descriptor.kind),
                            descriptor.blockType, descriptor.mode});
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
    {"Clean Twin", "models/clean.nam", "amps"},
    {"British Crunch", "models/crunch.nam", "amps"},
    {"Focused Lead", "models/solo.nam", "amps"},
    {"Open Back 2x12", "irs/open-back.wav", "cabs"},
    {"Vintage 4x12", "irs/vintage.wav", "cabs"},
    {"Focused 1x12", "irs/focus.wav", "cabs"},
    {"Compressor", "", "dynamics", "dynamics", "compressor"},
    {"Five Band EQ", "", "eq", "eq", "parametric_eq_5"},
  };
  appendDaisyAssets(state);
  return state;
}

void setActivePreset(UiState& state, std::size_t index, bool requestAudioSwap)
{
  if (index >= state.bank.presets.size()) {
    return;
  }
  state.activePreset = index;
  state.selectedBlock = 0;
  if (requestAudioSwap) {
    state.pendingSlotRequest = static_cast<int>(index);
  }
  enterPresetMode(state);
  state.dirty = false;
  state.effectsBypassed = false;
  state.blockEditUndo.reset();
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
  state.blockDrawerOpen = true;
  state.paramDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers | UiChange::Parameters);
}

void closeBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = false;
  markUiChanged(state, UiChange::Drawers);
}

void selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  state.selectedBlock = blockIndex;
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
  if (assetIndex >= state.assets.size()) {
    return;
  }

  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blocks.size() >= kMaxEffectBlocks) {
    return;
  }

  rememberBlockEdit(state);

  const auto& asset = state.assets[assetIndex];
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
    } else if (asset.blockType == "eq" && asset.mode == "parametric_eq_5") {
      params = parametricEqParamsToJson(defaultParametricEqParams());
    }
  }

  params = paramsWithKnownDefaults(type, params);

  const auto insertAt = std::min(blockIndex, blocks.size());
  blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(insertAt),
                {nextBlockId(blocks), type, label, asset.name, asset.path, true, params});
  state.selectedBlock = insertAt;
  state.paramTarget = UiParamTarget::Block;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = true;
  state.dirty = true;
  state.requiresEngineReload = true;
  setUiStatus(state, asset.name + " added - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
}

void moveBlock(UiState& state, std::size_t from, std::size_t to)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (from >= blocks.size() || to >= blocks.size() || from == to) {
    return;
  }

  rememberBlockEdit(state);
  const std::string movedName = blocks[from].assetName;
  auto block = std::move(blocks[from]);
  blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(from));
  blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(to), std::move(block));
  state.selectedBlock = to;
  state.dirty = true;
  state.requiresEngineReload = true;
  setUiStatus(state, movedName + " moved - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters);
}

bool deleteSelectedBlock(UiState& state)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return false;
  }

  rememberBlockEdit(state);
  const std::string deletedName = blocks[state.selectedBlock].assetName;
  blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(state.selectedBlock));
  if (blocks.empty()) {
    state.selectedBlock = 0;
    state.paramDrawerOpen = false;
  } else {
    state.selectedBlock = std::min(state.selectedBlock, blocks.size() - 1);
    state.paramTarget = UiParamTarget::Block;
  }
  state.dirty = true;
  state.requiresEngineReload = true;
  state.paramDrawerOpen = false;
  setUiStatus(state, deletedName + " deleted - Undo");
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters | UiChange::Drawers);
  return true;
}

bool undoLastBlockEdit(UiState& state)
{
  if (!state.blockEditUndo.has_value()) {
    return false;
  }
  auto snapshot = std::move(*state.blockEditUndo);
  state.blockEditUndo.reset();
  state.bank.presets[state.activePreset].blocks = std::move(snapshot.blocks);
  state.selectedBlock = snapshot.selectedBlock;
  state.paramTarget = snapshot.paramTarget;
  state.dirty = snapshot.dirty;
  state.requiresEngineReload = snapshot.requiresEngineReload;
  state.blockDrawerOpen = snapshot.blockDrawerOpen;
  state.paramDrawerOpen = snapshot.paramDrawerOpen;
  setUiStatus(state, "Change undone");
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
  static constexpr std::array valid = {
    "all", "amps", "cabs", "dynamics", "eq", "modulation", "delay", "reverb",
  };
  const auto found = std::find(valid.begin(), valid.end(), filter);
  state.categoryFilter = found == valid.end() ? "all" : std::move(filter);
  markUiChanged(state, UiChange::Drawers);
}

Preset activePresetToPreset(const UiState& state)
{
  Preset preset;
  const auto& uiPreset = state.bank.presets[state.activePreset];
  preset.name = uiPreset.name;
  preset.routing = "serial";
  preset.global = uiPreset.global;
  for (const auto& block : uiPreset.blocks) {
    preset.blocks.push_back({block.id, block.type, block.enabled, block.assetPath,
                             block.params.is_null() ? nlohmann::json::object() : block.params});
  }
  return preset;
}

void replaceActivePreset(UiState& state, const Preset& preset)
{
  auto& uiPreset = state.bank.presets[state.activePreset];
  uiPreset.name = preset.name;
  uiPreset.global = preset.global;
  uiPreset.blocks.clear();
  for (const auto& block : preset.blocks) {
    if (uiPreset.blocks.size() == kMaxEffectBlocks) {
      break;
    }
    uiPreset.blocks.push_back({block.id,
                               block.type,
                               labelForBlockType(block.type),
                               assetNameForBlock(state, block),
                               block.asset,
                               block.enabled,
                               paramsWithKnownDefaults(block.type, block.params)});
  }
  state.selectedBlock = 0;
  state.dirty = false;
  state.requiresEngineReload = false;
  state.paramDrawerOpen = false;
  state.blockDrawerOpen = false;
  state.blockEditUndo.reset();
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
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }
  blocks[state.selectedBlock].enabled = enabled;
  state.dirty = true;
  state.requiresEngineReload = true;
  markUiChanged(state, UiChange::Header | UiChange::Chain | UiChange::Parameters);
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
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }
  auto& block = blocks[state.selectedBlock];
  if (block.type == "cab") {
    if (key == "levelDb") {
      value = clampFloat(value, -60.0f, 12.0f);
    } else if (key == "mix") {
      value = clampFloat(value, 0.0f, 1.0f);
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
  }
  block.params[key] = value;
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

void setSelectedBlockParamValue(UiState& state, const std::string& key, nlohmann::json value)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }
  auto& block = blocks[state.selectedBlock];
  const bool compressorValue = block.type == "dynamics"
    && block.params.value("mode", "") == "compressor"
    && (key == "detector" || key == "auto_makeup");
  const bool namValue = block.type == "nam" && key == "useNano" && value.is_boolean();
  if (!compressorValue && !namValue) {
    return;
  }
  block.params[key] = std::move(value);
  state.dirty = true;
  state.requiresEngineReload = true;
  markUiChanged(state, UiChange::Header | UiChange::Parameters);
}

ParametricEqParams selectedParametricEqParams(const UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return defaultParametricEqParams();
  }
  const auto& block = blocks[state.selectedBlock];
  if (block.type != "eq" || !isParametricEqMode(block.params)) {
    return defaultParametricEqParams();
  }
  return parametricEqParamsFromJson(block.params);
}

bool setSelectedEqBand(UiState& state, std::size_t bandIndex, EqBandParams params)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size() || bandIndex >= kParametricEqBandCount) {
    return false;
  }
  auto& block = blocks[state.selectedBlock];
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

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry)
{
  const bool visibleChanged = state.telemetry.overBudget != telemetry.overBudget
    || state.telemetry.maxMs != telemetry.maxMs
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
  appendAssetsFrom(state, dataRoot / "models", ".nam", "amps");
  appendAssetsFrom(state, dataRoot / "irs", ".wav", "cabs");
  state.assets.push_back({"Compressor", "", "dynamics", "dynamics", "compressor"});
  state.assets.push_back({"Five Band EQ", "", "eq", "eq", "parametric_eq_5"});
  appendDaisyAssets(state);
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
  state.dirty = false;
  state.requiresEngineReload = false;
  state.paramDrawerOpen = false;
  state.blockDrawerOpen = false;
  state.blockEditUndo.reset();
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
