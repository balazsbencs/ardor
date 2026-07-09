#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"
#include "ui/UiModel.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int require(bool condition)
{
  return condition ? 0 : 1;
}

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

  if (require(ardor::consumePendingSlotRequest(state) == -1, "initial pending slot should be empty")) return 1;
  if (require(ardor::consumePendingSlotRequest(state) == -1, "consume is idempotent when empty")) return 1;
  ardor::selectPreset(state, 2);
  if (require(ardor::consumePendingSlotRequest(state) == 2, "select should post pending slot")) return 1;
  if (require(ardor::consumePendingSlotRequest(state) == -1, "consume clears the request")) return 1;
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

  replacement.global.inputGainDb = -6.0f;
  replacement.global.outputGainDb = -3.0f;
  replacement.global.safetyLimitDb = -1.5f;
  replacement.blocks[0].params = {{"levelDb", -4.0f}, {"mix", 0.75f}};
  ardor::replaceActivePreset(state, replacement);
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == -6.0f, "input global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].global.outputGainDb == -3.0f, "output global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].global.safetyLimitDb == -1.5f, "safety global should load")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[0].params.value("levelDb", 0.0f) == -4.0f,
              "block params should load")) return 1;
  ardor::selectGlobalParams(state);
  if (require(state.paramTarget == ardor::UiParamTarget::Globals, "global param drawer target")) return 1;
  if (require(state.paramDrawerOpen, "global params should open drawer")) return 1;

  ardor::setActiveInputGainDb(state, 20.0f);
  ardor::setActiveOutputGainDb(state, -80.0f);
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == 12.0f, "input gain should clamp high")) return 1;
  if (require(state.bank.presets[state.activePreset].global.outputGainDb == -60.0f, "output gain should clamp low")) return 1;
  if (require(state.bank.presets[state.activePreset].global.safetyLimitDb == -1.5f, "safety limit round-trips but has no UI setter")) return 1;
  if (require(state.dirty, "global edit should dirty preset")) return 1;

  state.dirty = false;
  ardor::setSelectedBlockParam(state, "mix", 0.25f);
  const auto editedPreset = ardor::activePresetToPreset(state);
  if (require(editedPreset.global.inputGainDb == 12.0f, "saved input global should round-trip")) return 1;
  if (require(editedPreset.blocks[0].params.value("mix", 0.0f) == 0.25f, "saved block params should round-trip")) return 1;
  if (require(state.dirty, "block param edit should dirty preset")) return 1;

  ardor::setSelectedBlockParam(state, "levelDb", -2.0f);
  ardor::setSelectedBlockParam(state, "mix", 0.5f);
  const auto cabParamPreset = ardor::activePresetToPreset(state);
  if (require(cabParamPreset.blocks[0].params.value("levelDb", 0.0f) == -2.0f, "cab level should save")) return 1;
  if (require(cabParamPreset.blocks[0].params.value("mix", 0.0f) == 0.5f, "cab mix should save")) return 1;

  ardor::insertAssetBlock(state, 0, 0);
  ardor::insertAssetBlock(state, 0, 0);
  if (require(state.bank.presets[state.activePreset].blocks[0].id != state.bank.presets[state.activePreset].blocks[1].id,
              "inserted block ids should be unique")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[0].id != state.bank.presets[state.activePreset].blocks[2].id,
              "inserted ids should not collide with loaded ids")) return 1;

  ardor::enterPresetMode(state);
  if (require(state.mode == ardor::UiMode::Preset, "preset mode should be active")) return 1;
  if (require(!state.blockDrawerOpen && !state.paramDrawerOpen, "preset mode should close drawers")) return 1;

  const auto root = std::filesystem::temp_directory_path() / "ardor-ui-model-smoke";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "models");
  std::filesystem::create_directories(root / "irs");
  {
    std::ofstream(root / "models/clean.nam").put('\n');
    std::ofstream(root / "irs/open.wav").put('\n');
  }

  ardor::PresetStore store(root);
  ardor::Preset diskPreset;
  diskPreset.name = "Disk Clean";
  diskPreset.blocks.push_back({"amp-1", "nam", true, "models/clean.nam", nlohmann::json::object()});
  store.save({0, 0}, diskPreset);

  auto diskState = ardor::makeDemoUiState();
  ardor::loadAssetsFromDataRoot(diskState, root);
  if (require(diskState.assets.size() >= 2, "data root assets should load")) return 1;
  if (require(diskState.assets[0].name == "clean", "model asset should use file stem")) return 1;

  ardor::loadBankFromStore(diskState, store, 0);
  if (require(diskState.bank.name == "Bank 000", "disk bank name")) return 1;
  if (require(diskState.bank.presets[0].name == "Disk Clean", "bank slot should load preset")) return 1;
  if (require(diskState.bank.presets[1].name == "Empty 2", "missing slot should become empty")) return 1;

  ardor::appendAssetBlock(diskState, 1);
  std::string ioError;
  if (require(ardor::saveActivePresetToStore(diskState, store, 0, ioError), "active preset save should succeed")) return 1;
  if (require(!diskState.dirty, "saving should clear dirty flag")) return 1;
  const auto saved = store.load({0, 0});
  if (require(saved.blocks.size() == 2, "saved preset should include edited chain")) return 1;
  std::filesystem::remove_all(root);

  ardor::RuntimeTelemetry telemetry = ardor::makeRuntimeTelemetry(1000, 2, 0.9, 0.3, 1.33, true);
  ardor::updateRealtimeTelemetry(diskState, telemetry);
  if (require(diskState.telemetry.callbacks == 1000, "ui telemetry callbacks")) return 1;
  if (require(diskState.effectsBypassed, "ui bypass follows telemetry")) return 1;

  return 0;
}
