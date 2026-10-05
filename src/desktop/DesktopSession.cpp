#include "desktop/DesktopSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <utility>

namespace ardor {

DesktopSession::DesktopSession(std::filesystem::path root)
  : root_(std::move(root)), store_(root_), state_(makeDemoUiState())
{
  initializeDesktopLibrary(root_);
  std::string settingsError;
  try { settings_ = loadDesktopSettings(root_); }
  catch (const std::exception& exception) { settingsError = exception.what(); }
  try { state_.settings = GlobalSettingsStore(root_).load(); }
  catch (const std::exception& exception) { settingsError += std::string(" ") + exception.what(); }
  state_.settings.audioBlockSize = settings_.blockSize;
  loadAssetsFromDataRoot(state_, root_);
  loadBankFromStore(state_, store_, settings_.bank);
  std::string error;
  if (!loadPresetSlotFromStore(state_, store_, {settings_.bank, settings_.slot}, error)) {
    setUiStatus(state_, "Cannot load the last preset: " + error, true);
    audioMessage_ = "Preset unavailable. Select another preset or repair its assets.";
    return;
  }
  if (!settingsError.empty()) setUiStatus(state_, "Settings could not be read: " + settingsError, true);
  prepare(activePresetToPreset(state_), {settings_.bank, static_cast<std::size_t>(settings_.slot)},
          PreparationKind::Initial);
}

DesktopSession::~DesktopSession()
{
  backend_.stop();
  // The worker captures values, not this session. Joining prevents prepared
  // DSP state from outliving shutdown; no LVGL objects are used by the worker.
  if (preparation_.valid()) preparation_.wait();
}

void DesktopSession::prepare(Preset preset, UiNavigationTarget target, PreparationKind kind)
{
  EngineLoadOptions options;
  options.blockSize = settings_.blockSize;
  options.parallelRigs = false; // Portable sequential execution, including WDW.
  const auto root = root_;
  preparation_ = std::async(std::launch::async,
    [preset = std::move(preset), root, options, target, kind]() mutable {
      Prepared result{std::make_unique<PedalEngine>(), std::move(preset), target, kind, {}};
      try {
        if (!applyPreset(*result.engine, result.preset, root, options, result.error)) result.engine.reset();
      } catch (const std::exception& exception) {
        result.error = exception.what();
        result.engine.reset();
      }
      return result;
    });
}

void DesktopSession::failPreparation(const Prepared& prepared, const std::string& error)
{
  if (prepared.kind == PreparationKind::Preview) failStructuralPreview(state_, error);
  else setUiStatus(state_, "Preset could not be prepared: " + error, true);
  if (!playing_) audioMessage_ = "Audio stopped. " + error;
}

void DesktopSession::tick()
{
  if (playing_ && backend_.deviceStopped()) {
    stopAudio();
    audioMessage_ = "Audio interface disconnected. Reconnect it, then press Start audio.";
    setUiStatus(state_, audioMessage_, true);
  }
  if (preparation_.valid() && preparation_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    auto prepared = preparation_.get();
    if (!prepared.engine) {
      failPreparation(prepared, prepared.error);
    } else {
      prepared.engine->setMasterVolume(static_cast<float>(state_.masterVolume) / 100.0f);
      auto result = EngineReplaceResult::Activated;
      if (playing_) result = backend_.replaceEngine(*prepared.engine);
      if (result == EngineReplaceResult::Activated) {
        engine_ = std::move(prepared.engine);
        engineBlockSize_ = settings_.blockSize;
        if (prepared.kind == PreparationKind::Preview) completeStructuralPreview(state_);
        if (prepared.kind == PreparationKind::Navigation) {
          if (state_.activeBank != prepared.target.bank) loadBankFromStore(state_, store_, prepared.target.bank);
          synchronizePresetSelection(state_, prepared.target.preset);
          replaceActivePreset(state_, prepared.preset);
          state_.dirty = false;
          state_.pendingSlotRequest = -1;
          settings_.bank = prepared.target.bank;
          settings_.slot = static_cast<int>(prepared.target.preset);
          persistSelection();
        }
        snapshotSerial_ = 0;
        sceneRequest_ = 0;
        if (tunerEnabled_) backend_.setOutputMuted(true);
        if (!playing_) audioMessage_ = "Audio stopped. Ready to play at 48 kHz.";
      } else {
        if (result == EngineReplaceResult::DeviceStopped || result == EngineReplaceResult::TimedOut) stopAudio();
        failPreparation(prepared, "Audio changed while loading. Press Start audio to retry.");
      }
    }
  }
  if (!busy() && pendingStructuralPreview(state_)) {
    prepare(activePresetToPreset(state_), {state_.activeBank, state_.activePreset}, PreparationKind::Preview);
  }
  if (engine_) engine_->setMasterVolume(static_cast<float>(state_.masterVolume) / 100.0f);
  if (!playing_ || !engine_) return;

  std::array<float, 2048> captured{};
  // Drain a bounded amount each UI tick, even if the device produces faster
  // than expected. Tuner analysis and metering never execute on the callback.
  for (int pass = 0; pass < 4; ++pass) {
    const auto count = backend_.readCapturedInput(captured.data(), captured.size());
    if (!count) break;
    if (tunerEnabled_) tuner_.process(captured.data(), count);
    if (count < captured.size()) break;
  }
  if (tunerEnabled_) {
    const auto& reading = tuner_.reading();
    updateTunerTelemetry(state_, {reading.signalDetected, reading.frequencyHz, reading.cents,
                                 reading.confidence, reading.note, reading.octave});
  }
  const auto now = std::chrono::steady_clock::now();
  if (now < nextTelemetry_) return;
  nextTelemetry_ = now + std::chrono::milliseconds(100);
  const auto stats = backend_.stats();
  updateRealtimeTelemetry(state_, makeRuntimeTelemetry(stats.callbacks, stats.overBudget,
    stats.callbackGaps, stats.maxMs, stats.averageMs, stats.budgetMs, false,
    engine_->parallelWaitOverBudgetCount(), engine_->nonFiniteBlockCount(),
    engine_->blockSizeMismatchCount(), -1.0, engine_->parallelUnderflowCount(),
    engine_->parallelSubmissionMissCount(), engine_->parallelWorkersReady()));
  if (state_.paramDrawerOpen) {
    if (const auto* selected = selectedUiBlock(state_)) {
      if (selected->params.value("mode", std::string{}) == "compressor") {
        updateCompressorGainReduction(state_, engine_->compressorGainReductionDb(selected->id));
      }
    }
  }
  if (engine_->scenesPrepared()) {
    const auto scene = engine_->sceneTransitionTelemetry();
    const float progress = scene.totalFrames == 0 ? 1.0f : std::clamp(
      static_cast<float>(scene.elapsedFrames) / scene.totalFrames, 0.0f, 1.0f);
    updateSceneTelemetry(state_, {scene.currentSceneIndex, scene.destinationSceneIndex,
      progress, scene.lastAppliedRequestId < sceneRequest_, scene.transitioning,
      state_.scenes.altered, false, state_.scenes.rejection});
    if (state_.sceneCapturePending) {
      std::vector<float> values;
      if (engine_->tryReadSceneValueSnapshot(snapshotSerial_, values)) completeCurrentSoundCapture(state_, values);
    }
  }
}

bool DesktopSession::startAudio()
{
  if (playing_) return true;
  if (busy() || pendingStructuralPreview(state_)) {
    audioMessage_ = "Preparing the preset. Press Start audio when it is ready.";
    return false;
  }
  if (!engine_ || engineBlockSize_ != settings_.blockSize) {
    audioMessage_ = "Choose a working preset before starting audio.";
    return false;
  }
  if (settings_.captureDeviceId.empty() || settings_.playbackDeviceId.empty()) {
    audioMessage_ = "Select input and output interfaces in Audio setup first.";
    return false;
  }
  RealtimeOptions options;
  options.blockSize = settings_.blockSize;
  options.captureDeviceId = settings_.captureDeviceId;
  options.playbackDeviceId = settings_.playbackDeviceId;
  options.inputChannel = settings_.inputChannel;
  options.requireNativeSampleRate = true;
  backend_.setOutputMuted(tunerEnabled_);
  if (!backend_.start(*engine_, options)) {
    audioMessage_ = backend_.lastError();
    setUiStatus(state_, audioMessage_, true);
    return false;
  }
  playing_ = true;
  audioMessage_ = "Audio running at 48 kHz / " + std::to_string(settings_.blockSize) + " frames.";
  setUiStatus(state_, "Audio started");
  return true;
}

void DesktopSession::stopAudio()
{
  backend_.stop();
  playing_ = false;
  tuner_.reset();
  updateTunerTelemetry(state_, {});
  updateRealtimeTelemetry(state_, {});
  audioMessage_ = "Audio stopped.";
}

bool DesktopSession::configureAudio(DesktopSettings settings, std::string& error)
{
  error.clear();
  if (busy() || !previewIsSynchronized(state_)) {
    error = "Wait for the preset to finish loading before changing audio settings.";
    return false;
  }
  settings.bank = settings_.bank;
  settings.slot = settings_.slot;
  if (!validateDesktopSettings(settings, error)) return false;
  try { saveDesktopSettings(root_, settings); }
  catch (const std::exception& exception) { error = exception.what(); return false; }
  stopAudio();
  const bool changedBlockSize = settings.blockSize != settings_.blockSize;
  settings_ = std::move(settings);
  state_.settings.audioBlockSize = settings_.blockSize;
  if (changedBlockSize || !engine_) {
    prepare(activePresetToPreset(state_), {state_.activeBank, state_.activePreset}, PreparationKind::BufferChange);
  }
  audioMessage_ = "Audio settings saved. Press Start audio to play.";
  return true;
}

void DesktopSession::navigate(UiNavigationTarget target)
{
  try { prepare(store_.loadOrEmpty({target.bank, static_cast<int>(target.preset)}), target, PreparationKind::Navigation); }
  catch (const std::exception& exception) { setUiStatus(state_, exception.what(), true); }
}

void DesktopSession::selectPreset(UiNavigationTarget target)
{
  if (busy()) { setUiStatus(state_, "Wait for the preset to finish loading"); return; }
  if (target.bank < 0 || target.bank >= 100 || target.preset >= 4) return;
  if (requestPresetNavigation(state_, target)) navigate(target);
}

void DesktopSession::resolveNavigation(UiNavigationDecision decision)
{
  if (busy()) return;
  if (decision == UiNavigationDecision::Save && !savePreset()) return;
  if (const auto target = confirmNavigation(state_, decision)) navigate(*target);
}

bool DesktopSession::savePreset()
{
  if (busy()) { setUiStatus(state_, "Wait for the preset to finish loading before saving"); return false; }
  std::string error;
  const bool saved = saveActivePresetToStore(state_, store_, state_.activeBank, error);
  setUiStatus(state_, saved ? "Preset saved" : "Could not save preset: " + error, !saved);
  return saved;
}

void DesktopSession::persistSelection()
{
  try { saveDesktopSettings(root_, settings_); }
  catch (const std::exception& exception) { setUiStatus(state_, "Could not remember this preset: " + std::string(exception.what()), true); }
}

void DesktopSession::setTuner(bool enabled)
{
  if (busy() || !previewIsSynchronized(state_)) return;
  tunerEnabled_ = enabled;
  tuner_.reset();
  backend_.setOutputMuted(enabled);
  if (enabled) enterTunerMode(state_);
  else enterPresetMode(state_);
}

void DesktopSession::selectScene(std::size_t index)
{
  if (busy() || !engine_ || !playing_ || index >= 4 || !previewIsSynchronized(state_)) return;
  const auto preset = activePresetToPreset(state_);
  if (!preset.sceneSet) return;
  const auto& scene = preset.sceneSet->scenes[index];
  const auto duration = scene.enterTimeMs * 48u;
  if (!engine_->tryRequestScene({engine_->scenePresetGeneration(), ++sceneRequest_, duration,
                                static_cast<std::uint8_t>(index)})) {
    setUiStatus(state_, "The scene could not be recalled", true);
  }
}

bool DesktopSession::updateParameter(const std::string& blockId, const std::string& key, float value)
{
  if (busy() || !engine_) return false;
  return engine_->setDaisyParameter(blockId, key, value)
    || engine_->setCompressorParameter(blockId, key, value)
    || engine_->setNoiseGateParameter(blockId, key, value)
    || engine_->setWahParameter(blockId, key, value)
    || engine_->setConsoleEqParameter(blockId, key, value)
    || engine_->setTransientShaperParameter(blockId, key, value)
    || engine_->setDistortionParameter(blockId, key, value)
    || engine_->setStereoWidenerParameter(blockId, key, value)
    || engine_->setIrReverbParameter(blockId, key, value)
    || engine_->setCabParameter(blockId, key, value);
}

} // namespace ardor
