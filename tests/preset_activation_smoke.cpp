#include "audio/PresetActivation.h"
#include "ui/UiModel.h"

#include <cmath>
#include <filesystem>
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
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "preset_activation_smoke failed: " << e.what() << '\n';
    return 1;
  }
}
