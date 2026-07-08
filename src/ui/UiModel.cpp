#include "ui/UiModel.h"

#include <algorithm>
#include <array>
#include <utility>

namespace ardor {

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
  state.paramDrawerOpen = true;
}

void appendAssetBlock(UiState& state, std::size_t assetIndex)
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
  blocks.push_back({"block-" + std::to_string(blocks.size() + 1), type, label, asset.name, asset.path, true});
  state.selectedBlock = blocks.size() - 1;
  state.dirty = true;
  state.paramDrawerOpen = true;
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

} // namespace ardor
