#include "ui/UiModel.h"

#include <iostream>

namespace {

int require(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  auto state = ardor::makeDemoUiState();
  if (require(state.bank.presets.size() == 4, "expected four presets")) return 1;
  if (require(state.bank.name == "Bank 000 - Core Sounds", "expected demo bank name")) return 1;
  if (require(state.activePreset == 0, "expected first preset active")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "expected preset mode")) return 1;
  if (require(state.masterVolume == 82, "expected demo master volume")) return 1;

  ardor::selectPreset(state, 2);
  if (require(state.activePreset == 2, "preset selection failed")) return 1;
  if (require(state.mode == ardor::UiMode::Preset, "preset selection should return to preset mode")) return 1;
  if (require(!state.blockDrawerOpen, "preset selection should close block drawer")) return 1;
  if (require(!state.paramDrawerOpen, "preset selection should close parameter drawer")) return 1;

  ardor::selectPreset(state, 99);
  if (require(state.activePreset == 2, "invalid preset selection should be ignored")) return 1;

  ardor::enterEditMode(state);
  if (require(state.mode == ardor::UiMode::Edit, "edit mode should be active")) return 1;
  ardor::openBlockDrawer(state);
  if (require(state.blockDrawerOpen, "block drawer should open")) return 1;
  ardor::setCategoryFilter(state, "modulation");
  if (require(state.categoryFilter == "modulation", "category filter failed")) return 1;
  ardor::closeBlockDrawer(state);
  if (require(!state.blockDrawerOpen, "block drawer close failed")) return 1;
  ardor::setCategoryFilter(state, "bogus");
  if (require(state.categoryFilter == "all", "bad filter should fall back to all")) return 1;

  ardor::selectBlock(state, 0);
  if (require(state.paramDrawerOpen, "block selection should open parameter drawer")) return 1;
  if (require(state.selectedBlock == 0, "selected block index failed")) return 1;
  ardor::closeParamDrawer(state);
  if (require(!state.paramDrawerOpen, "parameter drawer close failed")) return 1;

  const auto beforeAdd = state.bank.presets[state.activePreset].blocks.size();
  ardor::appendAssetBlock(state, 1);
  const auto& added = state.bank.presets[state.activePreset].blocks.back();
  if (require(state.bank.presets[state.activePreset].blocks.size() == beforeAdd + 1, "asset should append block")) return 1;
  if (require(added.assetName == "British Crunch", "added block should use asset name")) return 1;
  if (require(added.assetPath == "models/crunch.nam", "added block should use asset path")) return 1;
  if (require(state.dirty, "adding block should dirty preset")) return 1;
  if (require(state.selectedBlock == beforeAdd, "added block should be selected")) return 1;
  if (require(!state.paramDrawerOpen, "added block should not open parameter drawer")) return 1;

  const auto beforeInsert = state.bank.presets[state.activePreset].blocks.size();
  ardor::insertAssetBlock(state, 0, 1);
  const auto& inserted = state.bank.presets[state.activePreset].blocks[1];
  if (require(state.bank.presets[state.activePreset].blocks.size() == beforeInsert + 1, "asset should insert block")) return 1;
  if (require(inserted.assetName == "Clean Twin", "inserted block should use asset name")) return 1;
  if (require(inserted.assetPath == "models/clean.nam", "inserted block should use asset path")) return 1;
  if (require(state.selectedBlock == 1, "inserted block should be selected")) return 1;
  if (require(state.dirty, "inserting block should dirty preset")) return 1;
  if (require(!state.paramDrawerOpen, "inserted block should not open parameter drawer")) return 1;

  const auto firstBeforeMove = state.bank.presets[state.activePreset].blocks[0].id;
  const auto secondBeforeMove = state.bank.presets[state.activePreset].blocks[1].id;
  ardor::moveBlock(state, 0, 1);
  if (require(state.bank.presets[state.activePreset].blocks[0].id == secondBeforeMove, "target block should move forward")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[1].id == firstBeforeMove, "source block should land at target")) return 1;
  if (require(state.selectedBlock == 1, "moved block should stay selected")) return 1;
  if (require(state.dirty, "moving block should dirty preset")) return 1;

  const ardor::Preset savedPreset = ardor::activePresetToPreset(state);
  if (require(savedPreset.name == state.bank.presets[state.activePreset].name, "ui preset name maps to preset")) return 1;
  if (require(savedPreset.routing == "serial", "ui preset routing is serial")) return 1;
  if (require(savedPreset.blocks.size() == state.bank.presets[state.activePreset].blocks.size(), "ui blocks map to preset blocks")) return 1;
  if (require(savedPreset.blocks[0].asset == state.bank.presets[state.activePreset].blocks[0].assetPath, "ui block asset maps")) return 1;

  ardor::Preset replacement;
  replacement.name = "Loaded From Disk";
  replacement.blocks.push_back({"loaded-1", "cab", true, "irs/loaded.wav", nlohmann::json::object()});
  ardor::replaceActivePreset(state, replacement);
  if (require(state.bank.presets[state.activePreset].name == "Loaded From Disk", "preset load updates ui name")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks.size() == 1, "preset load updates ui block count")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[0].assetPath == "irs/loaded.wav", "preset load updates asset path")) return 1;
  if (require(!state.dirty, "loading preset clears dirty flag")) return 1;

  ardor::enterPresetMode(state);
  if (require(state.mode == ardor::UiMode::Preset, "preset mode should be active")) return 1;
  if (require(!state.blockDrawerOpen && !state.paramDrawerOpen, "preset mode should close drawers")) return 1;

  return 0;
}
