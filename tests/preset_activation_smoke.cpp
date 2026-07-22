#include "audio/PresetActivation.h"
#include "preset/PresetStore.h"
#include "ui/UiModel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ardor::Preset tremPreset(std::string name, std::string mode = "vintage_trem")
{
  ardor::Preset preset;
  preset.name = std::move(name);
  preset.blocks.push_back({"trem", "mod", true, "", {{"mode", std::move(mode)}}});
  return preset;
}

void requireFiniteOutput(ardor::PedalEngine& engine, const std::string& context)
{
  float input[64] = {};
  float left[64] = {};
  float right[64] = {};
  input[0] = 0.5f;
  engine.processBlock(input, left, right, 64);
  for (const float sample : left) {
    require(std::isfinite(sample), context + ": left output must remain finite");
  }
  for (const float sample : right) {
    require(std::isfinite(sample), context + ": right output must remain finite");
  }
}

std::string readFile(const std::filesystem::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

int main()
{
  try {
    const auto root = std::filesystem::temp_directory_path() / "ardor-preset-activation-smoke";
    const ardor::EngineLoadOptions options{48000, 64, 8192};
    auto liveEngine = std::make_unique<ardor::PedalEngine>();
    std::string error;
    require(ardor::applyPreset(*liveEngine, tremPreset("Active"), root, options, error), error);
    const auto originalEngine = liveEngine.get();
    ardor::ActivePresetSelection selection{3, 1};

    auto uiState = ardor::makeDemoUiState();
    uiState.activeBank = selection.bank;
    ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(selection.slot));
    require(ardor::consumePendingSlotRequest(uiState) == -1, "initial UI selection must be committed");

    int replaceCalls = 0;
    const auto failedPreparation = ardor::prepareAndActivatePreset(
      liveEngine, selection, tremPreset("Invalid Daisy", "bogus"), {3, 2}, root, options, 0.8f,
      [&](ardor::PedalEngine&) {
        ++replaceCalls;
        return ardor::EngineReplaceResult::Activated;
      });
    require(failedPreparation.status == ardor::PresetActivationStatus::PreparationFailed,
            "invalid Daisy target must fail during replacement preparation");
    require(replaceCalls == 0, "backend must not see an unprepared Daisy target");
    require(liveEngine.get() == originalEngine, "failed target preparation must retain the actual live engine");
    require(selection.bank == 3 && selection.slot == 1,
            "failed target preparation must retain the committed audio selection");
    require(uiState.activeBank == 3 && uiState.activePreset == 1,
            "failed target preparation must retain the visible UI selection");
    requireFiniteOutput(*liveEngine, "failed target preparation");

    const auto deviceLoss = ardor::prepareAndActivatePreset(
      liveEngine, selection, tremPreset("Device-loss target"), {4, 0}, root, options, 0.8f,
      [&](ardor::PedalEngine&) {
        ++replaceCalls;
        return ardor::EngineReplaceResult::DeviceStopped;
      });
    require(deviceLoss.status == ardor::PresetActivationStatus::BackendRejected,
            "device loss during swap must reject the uncommitted target");
    require(deviceLoss.replacementResult == ardor::EngineReplaceResult::DeviceStopped,
            "device loss must be visible to the control loop for requeue");
    require(replaceCalls == 1, "prepared target should reach the backend exactly once");
    require(liveEngine.get() == originalEngine, "device loss must retain the live engine for recovery");
    require(selection.bank == 3 && selection.slot == 1,
            "device loss must not commit the target selection before recovery");
    require(uiState.activeBank == 3 && uiState.activePreset == 1,
            "device loss must not move the UI ahead of audible audio");
    requireFiniteOutput(*liveEngine, "device-loss recovery");

    const auto activated = ardor::prepareAndActivatePreset(
      liveEngine, selection, tremPreset("Activated target"), {4, 0}, root, options, 0.65f,
      [&](ardor::PedalEngine&) {
        ++replaceCalls;
        return ardor::EngineReplaceResult::Activated;
      });
    require(activated.activated(), "backend acknowledgement should commit the prepared target");
    require(replaceCalls == 2, "successful target should reach the backend exactly once");
    require(liveEngine.get() != originalEngine, "successful activation must replace the live engine");
    require(selection.bank == 4 && selection.slot == 0,
            "audio selection changes only after backend acknowledgement");

    // This is the production post-activation UI operation. It cannot enqueue a
    // second swap, so visual state follows the newly committed audio state.
    uiState.activeBank = selection.bank;
    ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(selection.slot));
    require(uiState.activeBank == 4 && uiState.activePreset == 0,
            "UI selection must match the committed audio selection");
    require(ardor::consumePendingSlotRequest(uiState) == -1,
            "post-activation UI synchronization must not request a redundant swap");
    requireFiniteOutput(*liveEngine, "successful activation");

    // Exercise the same draft -> prepared-engine lifecycle used by the live
    // editor. The added block must immediately accept atomic updates after its
    // preview completes; delete and Undo must update the runtime too.
    auto previewUi = ardor::makeDemoUiState();
    previewUi.activeBank = 8;
    ardor::synchronizePresetSelection(previewUi, 2);
    ardor::replaceActivePreset(previewUi, tremPreset("Preview base"));
    auto tremAsset = std::find_if(previewUi.assets.begin(), previewUi.assets.end(), [](const ardor::UiAsset& asset) {
      return asset.name == "Vintage Trem";
    });
    require(tremAsset != previewUi.assets.end(), "preview test needs the Vintage Trem asset");
    auto previewEngine = std::make_unique<ardor::PedalEngine>();
    require(ardor::applyPreset(*previewEngine, ardor::activePresetToPreset(previewUi), root, options, error), error);
    ardor::ActivePresetSelection previewSelection{8, 2};
    ardor::PresetStore previewStore(root);
    previewStore.save({previewSelection.bank, previewSelection.slot}, ardor::activePresetToPreset(previewUi));
    const auto previewPath = previewStore.pathFor({previewSelection.bank, previewSelection.slot});
    const auto persistedBeforePreview = readFile(previewPath);
    int previewReplacements = 0;
    const auto activatePreview = [&]() {
      require(ardor::beginApplyingPreview(previewUi), "queued live edit should begin applying");
      const auto outcome = ardor::prepareAndActivateDraft(
        previewEngine, ardor::activePresetToPreset(previewUi), root, options, 0.7f,
        [&](ardor::PedalEngine&) {
          ++previewReplacements;
          return ardor::EngineReplaceResult::Activated;
        });
      require(outcome.activated(), "live edit preview should activate");
      require(previewSelection.bank == 8 && previewSelection.slot == 2,
              "draft activation must not alter the committed preset selection");
      ardor::completeStructuralPreview(previewUi);
    };
    ardor::appendAssetBlock(previewUi, static_cast<std::size_t>(std::distance(previewUi.assets.begin(), tremAsset)));
    require(previewUi.previewState == ardor::UiPreviewState::Queued,
            "adding an effect should queue a live preview");
    const std::string addedId = previewUi.bank.presets[previewUi.activePreset].blocks.back().id;
    activatePreview();
    require(previewEngine->setDaisyParameter(addedId, "depth", 0.25f),
            "added effect must accept live parameters after activation");
    require(readFile(previewPath) == persistedBeforePreview,
            "successful preview must not write preset storage");
    const auto engineBeforeSave = previewEngine.get();
    require(ardor::saveActivePresetToStore(previewUi, previewStore, previewSelection.bank, error),
            "saving a synchronized preview should succeed");
    require(previewEngine.get() == engineBeforeSave && previewReplacements == 1,
            "Save must persist without another engine activation");
    require(readFile(previewPath) != persistedBeforePreview,
            "Save must persist the already-audible preview draft");

    require(ardor::deleteSelectedBlock(previewUi), "delete should queue a live preview");
    activatePreview();
    require(!previewEngine->setDaisyParameter(addedId, "depth", 0.5f),
            "deleted effect must no longer exist in the runtime chain");

    require(ardor::undoLastBlockEdit(previewUi), "Undo should queue restoration preview");
    activatePreview();
    require(previewEngine->setDaisyParameter(addedId, "depth", 0.5f),
            "Undo must restore the effect to the runtime chain");
    require(previewReplacements == 3, "each structural preview should activate exactly once");

    const auto engineBeforeRejectedPreview = previewEngine.get();
    const auto rejectedRollback = ardor::captureUiPreviewSnapshot(previewUi);
    previewUi.bank.presets[previewUi.activePreset].blocks.front().params["mode"] = "bogus";
    previewUi.dirty = true;
    require(ardor::queuePreview(previewUi, rejectedRollback, "invalid effect"),
            "invalid candidate should still enter preview flow");
    require(ardor::beginApplyingPreview(previewUi), "invalid candidate should begin applying");
    const auto rejected = ardor::prepareAndActivateDraft(
      previewEngine, ardor::activePresetToPreset(previewUi), root, options, 0.7f,
      [&](ardor::PedalEngine&) {
        ++previewReplacements;
        return ardor::EngineReplaceResult::Activated;
      });
    require(rejected.status == ardor::PresetActivationStatus::PreparationFailed,
            "invalid candidate must fail before backend activation");
    ardor::failStructuralPreview(previewUi, rejected.error);
    require(previewEngine.get() == engineBeforeRejectedPreview
              && previewUi.bank.presets[previewUi.activePreset].blocks.front().params.value("mode", "")
                   == "vintage_trem",
            "failed preview must retain the audible engine and restore the complete draft");
    require(previewReplacements == 3, "failed preparation must not invoke the backend");

    const int replacementsBeforeStress = previewReplacements;
    for (int edit = 0; edit < 100; ++edit) {
      switch (edit % 4) {
      case 0: {
        const auto& blocks = previewUi.bank.presets[previewUi.activePreset].blocks;
        const auto added = std::find_if(blocks.begin(), blocks.end(), [&](const ardor::UiBlock& block) {
          return block.id == addedId;
        });
        require(added != blocks.end(), "stress chain must retain the added block");
        ardor::selectBlock(previewUi, static_cast<std::size_t>(std::distance(blocks.begin(), added)));
        ardor::setSelectedBlockEnabled(previewUi, !added->enabled);
        break;
      }
      case 1:
        ardor::moveBlock(previewUi, 0, 1);
        break;
      case 2:
        require(ardor::undoLastBlockEdit(previewUi), "stress Undo should be available after reorder");
        break;
      case 3:
        ardor::moveBlock(previewUi, 1, 0);
        break;
      }
      require(previewUi.previewState == ardor::UiPreviewState::Queued,
              "each stress edit should queue one preview");
      activatePreview();
      requireFiniteOutput(*previewEngine, "structural preview stress");
    }
    require(previewReplacements == replacementsBeforeStress + 100,
            "stress edits should produce exactly one activation each");
    requireFiniteOutput(*previewEngine, "live edit preview lifecycle");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "preset_activation_smoke failed: " << e.what() << '\n';
    return 1;
  }
}
