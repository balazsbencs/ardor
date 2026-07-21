#include "audio/PresetActivation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ardor {

PresetActivationOutcome prepareAndActivatePreset(
  std::unique_ptr<PedalEngine>& liveEngine,
  ActivePresetSelection& activeSelection,
  const Preset& targetPreset,
  ActivePresetSelection targetSelection,
  const std::filesystem::path& dataRoot,
  const EngineLoadOptions& options,
  float masterVolume,
  const EngineReplaceCallback& replaceEngine)
{
  PresetActivationOutcome outcome;
  auto nextEngine = std::make_unique<PedalEngine>();
  if (!applyPreset(*nextEngine, targetPreset, dataRoot, options, outcome.error)) {
    outcome.status = PresetActivationStatus::PreparationFailed;
    return outcome;
  }

  nextEngine->setMasterVolume(std::isfinite(masterVolume) ? std::max(0.0f, masterVolume) : 1.0f);
  outcome.replacementResult = replaceEngine(*nextEngine);
  if (outcome.replacementResult != EngineReplaceResult::Activated) {
    outcome.status = PresetActivationStatus::BackendRejected;
    return outcome;
  }

  liveEngine = std::move(nextEngine);
  activeSelection = targetSelection;
  outcome.status = PresetActivationStatus::Activated;
  return outcome;
}

} // namespace ardor
