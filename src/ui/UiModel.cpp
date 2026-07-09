#include "ui/UiModel.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ardor {

namespace {

std::string labelForBlockType(const std::string& type)
{
  if (type == "nam") {
    return "Neural Amp";
  }
  if (type == "cab") {
    return "Cab";
  }
  return type;
}

std::string assetNameForPath(const UiState& state, const std::string& path, const std::string& type)
{
  for (const auto& asset : state.assets) {
    if (asset.path == path) {
      return asset.name;
    }
  }
  return path.empty() ? type : path;
}

float clampFloat(float value, float low, float high)
{
  return std::clamp(value, low, high);
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

void appendAssetsFrom(UiState& state, const std::filesystem::path& dir, const std::string& ext, const std::string& type)
{
  if (!std::filesystem::exists(dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ext) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), dir.parent_path()).generic_string();
    state.assets.push_back({entry.path().stem().string(), relative, type});
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
                         {"block-6", "mod", "Chorus", "Wide Chorus", "", true},
                         {"block-7", "time", "Delay", "Tape Delay", "", true}}},
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
    {"Compressor", "", "dynamics"},
    {"Wide Chorus", "", "modulation"},
    {"Tape Delay", "", "time"},
  };
  return state;
}

void selectPreset(UiState& state, std::size_t index)
{
  if (index >= state.bank.presets.size()) {
    return;
  }
  state.activePreset = index;
  state.selectedBlock = 0;
  state.pendingSlotRequest = static_cast<int>(index);
  enterPresetMode(state);
  state.dirty = false;
  state.effectsBypassed = false;
}

void enterPresetMode(UiState& state)
{
  state.mode = UiMode::Preset;
  state.blockDrawerOpen = false;
  state.paramDrawerOpen = false;
}

void enterEditMode(UiState& state)
{
  state.mode = UiMode::Edit;
}

void openBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = true;
}

void closeBlockDrawer(UiState& state)
{
  state.blockDrawerOpen = false;
}

void selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  state.selectedBlock = blockIndex;
  state.paramTarget = UiParamTarget::Block;
  state.paramDrawerOpen = true;
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

  const auto& asset = state.assets[assetIndex];
  std::string type = asset.type;
  std::string label = asset.name;
  if (asset.type == "amps") {
    type = "nam";
    label = "Neural Amp";
  } else if (asset.type == "cabs") {
    type = "cab";
    label = "Cab";
  }

  auto& blocks = state.bank.presets[state.activePreset].blocks;
  const auto insertAt = std::min(blockIndex, blocks.size());
  blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(insertAt),
                {nextBlockId(blocks), type, label, asset.name, asset.path, true});
  state.selectedBlock = insertAt;
  state.dirty = true;
}

void moveBlock(UiState& state, std::size_t from, std::size_t to)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (from >= blocks.size() || to >= blocks.size() || from == to) {
    return;
  }

  auto block = std::move(blocks[from]);
  blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(from));
  blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(to), std::move(block));
  state.selectedBlock = to;
  state.dirty = true;
}

void closeParamDrawer(UiState& state)
{
  state.paramDrawerOpen = false;
}

void setCategoryFilter(UiState& state, std::string filter)
{
  static constexpr std::array valid = {"all", "amps", "cabs", "dynamics", "modulation", "time"};
  const auto found = std::find(valid.begin(), valid.end(), filter);
  state.categoryFilter = found == valid.end() ? "all" : std::move(filter);
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
    uiPreset.blocks.push_back({block.id,
                               block.type,
                               labelForBlockType(block.type),
                               assetNameForPath(state, block.asset, block.type),
                               block.asset,
                               block.enabled,
                               block.params.is_null() ? nlohmann::json::object() : block.params});
  }
  state.selectedBlock = 0;
  state.dirty = false;
  state.paramDrawerOpen = false;
}

void selectGlobalParams(UiState& state)
{
  state.paramTarget = UiParamTarget::Globals;
  state.paramDrawerOpen = true;
}

void setActiveInputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.inputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
}

void setActiveOutputGainDb(UiState& state, float db)
{
  state.bank.presets[state.activePreset].global.outputGainDb = clampFloat(db, -60.0f, 12.0f);
  state.dirty = true;
}

void setSelectedBlockParam(UiState& state, const std::string& key, float value)
{
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }
  blocks[state.selectedBlock].params[key] = value;
  state.dirty = true;
}

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry)
{
  state.telemetry = telemetry;
  state.effectsBypassed = telemetry.bypassed;
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
  state.assets.push_back({"Compressor", "", "dynamics"});
  state.assets.push_back({"Wide Chorus", "", "modulation"});
  state.assets.push_back({"Tape Delay", "", "time"});
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
  state.paramDrawerOpen = false;
}

bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error)
{
  try {
    store.save({bank, static_cast<int>(state.activePreset)}, activePresetToPreset(state));
    state.dirty = false;
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

} // namespace ardor
