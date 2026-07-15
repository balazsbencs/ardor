#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"
#include "ui/ParameterControls.h"
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

  const int masterVolume = state.masterVolume;
  ardor::setActiveInputGainDb(state, 1.0f);
  if (require(state.masterVolume == masterVolume, "preset parameter edits should not change master volume")) return 1;
  state.dirty = false;

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

  auto tremAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Vintage Trem";
  });
  if (require(tremAsset != state.assets.end(), "daisy effect should be in asset list")) return 1;
  const auto tremIndex = static_cast<std::size_t>(std::distance(state.assets.begin(), tremAsset));
  const auto beforeTremAdd = state.bank.presets[state.activePreset].blocks.size();
  ardor::appendAssetBlock(state, tremIndex);
  const auto& trem = state.bank.presets[state.activePreset].blocks.back();
  if (require(state.bank.presets[state.activePreset].blocks.size() == beforeTremAdd + 1, "daisy asset should append block")) return 1;
  if (require(trem.type == "mod", "daisy block should use catalog block type")) return 1;
  if (require(trem.assetPath.empty(), "daisy block should not use asset path")) return 1;
  if (require(trem.params.value("mode", "") == "vintage_trem", "daisy block should use catalog mode")) return 1;
  if (require(trem.params.contains("depth"), "daisy block should include default params")) return 1;
  if (require(!state.paramDrawerOpen, "daisy add should not open parameter drawer")) return 1;

  ardor::selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ardor::setSelectedBlockParam(state, "depth", 0.25f);
  const auto tremPreset = ardor::activePresetToPreset(state);
  if (require(tremPreset.blocks.back().type == "mod", "daisy block should save as mod")) return 1;
  if (require(tremPreset.blocks.back().asset.empty(), "daisy block should save empty asset")) return 1;
  if (require(tremPreset.blocks.back().params.value("depth", 0.0f) == 0.25f, "daisy params should save")) return 1;
  ardor::closeParamDrawer(state);

  auto delayAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Digital Delay";
  });
  auto reverbAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Room Reverb";
  });
  if (require(delayAsset != state.assets.end(), "digital delay should be in asset list")) return 1;
  if (require(reverbAsset != state.assets.end(), "room reverb should be in asset list")) return 1;

  auto compressorAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Compressor";
  });
  if (require(compressorAsset != state.assets.end(), "compressor should be in asset list")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), compressorAsset)));
  const auto& compressor = state.bank.presets[state.activePreset].blocks.back();
  if (require(compressor.type == "dynamics", "compressor block should use dynamics type")) return 1;
  if (require(compressor.params.value("mode", "") == "compressor", "compressor block should set compressor mode")) return 1;
  if (require(compressor.params.contains("threshold_db") && compressor.params.contains("auto_makeup"),
              "compressor should include complete defaults")) return 1;
  ardor::selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  if (require(ardor::parameterPageCount(state) == 2, "compressor controls should paginate")) return 1;
  const auto compressorControls = ardor::parameterPage(state, 0);
  if (require(compressorControls.size() == 7 && compressorControls[0].label == "Threshold"
              && compressorControls[1].label == "Ratio", "compressor controls should have meaningful labels")) return 1;
  ardor::closeParamDrawer(state);

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

  while (state.bank.presets[state.activePreset].blocks.size() < ardor::kMaxEffectBlocks) {
    ardor::appendAssetBlock(state, 0);
  }
  const auto fullChainSize = state.bank.presets[state.activePreset].blocks.size();
  ardor::appendAssetBlock(state, 0);
  if (require(state.bank.presets[state.activePreset].blocks.size() == fullChainSize,
              "effect chain should not exceed ten blocks")) return 1;

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

  auto migrationState = ardor::makeDemoUiState();
  ardor::Preset legacyEffects;
  legacyEffects.name = "Legacy Effects";
  legacyEffects.blocks.push_back({"legacy-shimmer", "reverb", true, "", {{"mode", "shimmer"}}});
  legacyEffects.blocks.push_back({"legacy-compressor", "dynamics", true, "", {{"mode", "compressor"}}});
  ardor::replaceActivePreset(migrationState, legacyEffects);
  const auto& migratedShimmer = migrationState.bank.presets[migrationState.activePreset].blocks[0].params;
  const auto& migratedCompressor = migrationState.bank.presets[migrationState.activePreset].blocks[1].params;
  if (require(migratedShimmer.contains("decay") && migratedShimmer.contains("param2"),
              "legacy Daisy block should receive missing defaults")) return 1;
  if (require(migratedCompressor.contains("threshold_db") && migratedCompressor.contains("auto_makeup"),
              "legacy compressor should receive missing defaults")) return 1;

  auto eqAsset = std::find_if(migrationState.assets.begin(), migrationState.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Five Band EQ";
  });
  if (require(eqAsset != migrationState.assets.end(), "EQ should be in the asset list")) return 1;
  migrationState.dirty = false;
  ardor::appendAssetBlock(migrationState, static_cast<std::size_t>(std::distance(migrationState.assets.begin(), eqAsset)));
  const auto& eqBlock = migrationState.bank.presets[migrationState.activePreset].blocks.back();
  if (require(eqBlock.type == "eq" && eqBlock.label == "EQ", "EQ asset should create a native EQ block")) return 1;
  if (require(eqBlock.params.value("mode", "") == "parametric_eq_5", "EQ block should use the supported mode")) return 1;
  if (require(eqBlock.params.at("bands").size() == ardor::kParametricEqBandCount,
              "EQ block should include five bands")) return 1;
  if (require(migrationState.dirty, "adding an EQ block should dirty the preset")) return 1;

  const auto originalEq = ardor::selectedParametricEqParams(migrationState);
  auto editedBand = originalEq.bands[2];
  editedBand.enabled = false;
  editedBand.frequencyHz = 1200.0f;
  editedBand.q = 1.8f;
  editedBand.gainDb = 7.0f;
  migrationState.dirty = false;
  if (require(ardor::setSelectedEqBand(migrationState, 2, editedBand), "EQ band edit should succeed")) return 1;
  const auto editedEq = ardor::selectedParametricEqParams(migrationState);
  if (require(editedEq.bands[2] == editedBand, "EQ band edit should canonicalize and persist")) return 1;
  if (require(migrationState.dirty, "EQ band edit should dirty the preset")) return 1;
  if (require(ardor::resetSelectedEqBand(migrationState, 2), "EQ band reset should succeed")) return 1;
  if (require(ardor::selectedParametricEqParams(migrationState).bands[2] == ardor::defaultParametricEqBand(2),
              "EQ band reset should restore the indexed default")) return 1;

  const auto blocksBeforeDelete = migrationState.bank.presets[migrationState.activePreset].blocks.size();
  const auto deletedBlockId = migrationState.bank.presets[migrationState.activePreset].blocks[migrationState.selectedBlock].id;
  migrationState.dirty = false;
  if (require(ardor::deleteSelectedBlock(migrationState), "selected block delete should succeed")) return 1;
  if (require(migrationState.bank.presets[migrationState.activePreset].blocks.size() == blocksBeforeDelete - 1,
              "selected block delete should remove exactly one block")) return 1;
  if (require(std::none_of(migrationState.bank.presets[migrationState.activePreset].blocks.begin(),
                           migrationState.bank.presets[migrationState.activePreset].blocks.end(),
                           [&](const ardor::UiBlock& block) { return block.id == deletedBlockId; }),
              "selected block delete should remove the selected id")) return 1;
  if (require(migrationState.dirty, "selected block delete should dirty the preset")) return 1;

  ardor::Preset eqAssetNamePreset;
  eqAssetNamePreset.name = "EQ Asset Name";
  eqAssetNamePreset.blocks.push_back({"eq-asset", "eq", true, "",
                                      ardor::parametricEqParamsToJson(ardor::defaultParametricEqParams())});
  ardor::replaceActivePreset(migrationState, eqAssetNamePreset);
  if (require(migrationState.bank.presets[migrationState.activePreset].blocks[0].assetName == "Five Band EQ",
              "EQ blocks without file assets should retain the EQ asset name")) return 1;

  return 0;
}
