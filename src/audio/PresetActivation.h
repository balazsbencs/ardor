#pragma once

#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ardor {

// The committed selection describes the engine currently audible to the user.
// It is deliberately updated only after the backend accepts the prepared engine.
struct ActivePresetSelection {
  int bank = 0;
  int slot = 0;
};

enum class PresetActivationStatus {
  Activated,
  LooperLocked,
  LooperNotPaused,
  PreparationFailed,
  BackendRejected,
};

struct PresetActivationOutcome {
  PresetActivationStatus status = PresetActivationStatus::PreparationFailed;
  EngineReplaceResult replacementResult = EngineReplaceResult::DeviceStopped;
  std::string error;

  bool activated() const noexcept { return status == PresetActivationStatus::Activated; }
};

using EngineReplaceCallback = std::function<EngineReplaceResult(PedalEngine&)>;

// Prepares and activates an in-memory draft without changing the committed
// bank/slot selection. This is the on-device live-preview path.
PresetActivationOutcome prepareAndActivateDraft(
  std::unique_ptr<PedalEngine>& liveEngine,
  const Preset& draft,
  const std::filesystem::path& dataRoot,
  const EngineLoadOptions& options,
  float masterVolume,
  const EngineReplaceCallback& replaceEngine);

// Builds a complete replacement while liveEngine remains audible, then asks the
// backend to activate it. Preparation or backend failure leaves both liveEngine
// and activeSelection unchanged. DeviceStopped is returned to the caller so it
// can requeue the requested slot after its normal device-recovery path.
PresetActivationOutcome prepareAndActivatePreset(
  std::unique_ptr<PedalEngine>& liveEngine,
  ActivePresetSelection& activeSelection,
  const Preset& targetPreset,
  ActivePresetSelection targetSelection,
  const std::filesystem::path& dataRoot,
  const EngineLoadOptions& options,
  float masterVolume,
  const EngineReplaceCallback& replaceEngine);

// Recalls a saved loop as one prepared unit: stored preset, preallocated loop
// memory, and decoded track audio. The current engine remains audible unless
// the complete candidate is accepted by the backend. The current session must
// already be paused so Save/Load never silently interrupts a performance.
PresetActivationOutcome prepareAndActivateLoopSession(
  std::unique_ptr<PedalEngine>& liveEngine,
  const Preset& storedPreset,
  const LooperPausedSessionView& storedSession,
  const std::filesystem::path& dataRoot,
  const EngineLoadOptions& options,
  std::size_t looperMemoryBudgetBytes,
  float masterVolume,
  const EngineReplaceCallback& replaceEngine);

} // namespace ardor
