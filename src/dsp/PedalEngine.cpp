#include "PedalEngine.h"

#include "DenormalGuard.h"

#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DualAmpProcessor.h"
#include "dsp/DualRigProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "dynamics/NoiseGateProcessor.h"
#include "cheese/CheeseProcessor.h"
#include "dynamics/TransientShaperProcessor.h"
#include "tape/TapeProcessor.h"
#include "equalizer/EqParameters.h"
#include "wah/WahProcessor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ardor {

namespace {

void commitLevel(std::atomic<uint32_t>& peakBits, std::atomic<uint64_t>& overloadFrames,
                 float peak, uint64_t overloadCount)
{
  const uint32_t bits = std::bit_cast<uint32_t>(peak);
  uint32_t previous = peakBits.load(std::memory_order_relaxed);
  while (previous < bits
         && !peakBits.compare_exchange_weak(previous, bits, std::memory_order_relaxed)) {
  }
  if (overloadCount > 0) {
    overloadFrames.fetch_add(overloadCount, std::memory_order_relaxed);
  }
}

void observeLevel(std::atomic<uint32_t>& peakBits, std::atomic<uint64_t>& overloadFrames,
                  float left, float right)
{
  const float peak = std::max(std::fabs(left), std::fabs(right));
  commitLevel(peakBits, overloadFrames, peak, peak > 1.0f ? 1U : 0U);
}

ClipStageSnapshot takeLevel(std::atomic<uint32_t>& peakBits,
                            std::atomic<uint64_t>& overloadFrames,
                            SignalStageKind kind, std::string id)
{
  return {
    kind,
    std::move(id),
    std::bit_cast<float>(peakBits.exchange(0, std::memory_order_relaxed)),
    overloadFrames.exchange(0, std::memory_order_relaxed),
  };
}

std::string stageLabel(const ClipStageSnapshot& stage)
{
  std::string label = signalStageKindName(stage.kind);
  if (!stage.id.empty()) {
    label += ":" + stage.id;
  }
  return label;
}

} // namespace

const char* signalStageKindName(SignalStageKind kind) noexcept
{
  switch (kind) {
  case SignalStageKind::Input: return "input";
  case SignalStageKind::Nam: return "nam";
  case SignalStageKind::Cab: return "ir";
  case SignalStageKind::IrReverb: return "ir-reverb";
  case SignalStageKind::Daisy: return "daisy";
  case SignalStageKind::Compressor: return "compressor";
  case SignalStageKind::NoiseGate: return "noise-gate";
  case SignalStageKind::TransientShaper: return "transient-shaper";
  case SignalStageKind::Equalizer: return "eq";
  case SignalStageKind::Wah: return "wah";
  case SignalStageKind::Distortion: return "distortion";
  case SignalStageKind::StereoWidener: return "stereo-widener";
  case SignalStageKind::DualAmp: return "dual-amp";
  case SignalStageKind::DualRig: return "dual-rig";
  case SignalStageKind::Output: return "output";
  }
  return "unknown";
}

std::string formatClipDiagnostics(const ClipDiagnosticsSnapshot& diagnostics)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << "levels";
  std::string firstOverload;
  for (const auto& stage : diagnostics.stages) {
    const std::string label = stageLabel(stage);
    const float db = stage.peak > 0.0f ? 20.0f * std::log10(stage.peak) : -120.0f;
    out << ' ' << label << '=' << db << "dBFS";
    if (stage.overloadFrames > 0) {
      out << " CLIP[" << stage.overloadFrames << ']';
      if (firstOverload.empty()) {
        firstOverload = label;
      }
    }
  }
  if (diagnostics.limiterFrames > 0) {
    out << " limiter=[" << diagnostics.limiterFrames << ']';
  }
  if (!firstOverload.empty()) {
    out << " first=" << firstOverload;
  }
  return out.str();
}

void PedalEngine::setSampleRate(double sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    return;
  }
  sampleRate_ = static_cast<float>(sampleRate);
  constexpr float kGainSmoothingSeconds = 0.005f;
  gainSmoothingCoefficient_ = 1.0f - std::exp(-1.0f / (kGainSmoothingSeconds * sampleRate_));
}

bool PedalEngine::loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
                          std::string id, float slimmableSize, NamInputMode inputMode,
                          std::optional<float> inputReferenceLevelDbU)
{
  setSampleRate(sampleRate);
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addNam(modelPath, sampleRate, maxBlockSize, std::move(id), slimmableSize,
                       inputMode, inputReferenceLevelDbU);
}

bool PedalEngine::addDualAmp(std::string id, DualAmpLaneConfig left, DualAmpLaneConfig right,
                             NamInputMode inputMode, double sampleRate, int maxBlockSize,
                             bool requestParallel, int workerCpu, std::string& error)
{
  setSampleRate(sampleRate);
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addDualAmp(std::move(id), std::move(left), std::move(right), inputMode,
                           sampleRate, maxBlockSize, requestParallel, workerCpu, error);
}

bool PedalEngine::addDualRig(std::string id, DualRigLaneConfig left, DualRigLaneConfig right,
                             NamInputMode inputMode, double sampleRate, int maxBlockSize,
                             bool requestParallel, int workerCpu, std::string& error)
{
  setSampleRate(sampleRate);
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addDualRig(std::move(id), std::move(left), std::move(right), inputMode,
                           sampleRate, static_cast<std::size_t>(maxBlockSize),
                           requestParallel, workerCpu, error);
}

void PedalEngine::loadIr(std::vector<float> impulse)
{
  addCab(std::move(impulse), cabLevel_.load(std::memory_order_relaxed), cabMix_.load(std::memory_order_relaxed));
}

void PedalEngine::addCab(std::vector<float> impulse, float level, float mix, std::string id)
{
  const float safeLevel = std::isfinite(level) ? std::max(0.0f, level) : 1.0f;
  const float safeMix = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 1.0f;
  cabLevel_.store(safeLevel, std::memory_order_relaxed);
  cabMix_.store(safeMix, std::memory_order_relaxed);
  chain_.addCab(std::move(impulse), safeLevel, safeMix, std::move(id));
}

bool PedalEngine::addIrReverb(std::string id, std::vector<float> left, std::vector<float> right,
                              float sampleRate, std::string& error, bool sceneLetRing)
{
  return chain_.addIrReverb(std::move(id), std::move(left), std::move(right), sampleRate, error,
                            sceneLetRing);
}

bool PedalEngine::setIrReverbParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setIrReverbParameter(id, key, value)) return true;
  return chain_.setIrReverbParameter(id, key, value);
}

bool PedalEngine::setCabParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setCabParameter(id, key, value)) return true;
  if (!chain_.setCabParameter(id, key, value)) return false;
  // The legacy serial path supplies host-smoothed cabinet arrays to its chain,
  // so keep those targets synchronized after resolving the requested block ID.
  if (key == "mix") {
    setCabMix(value);
  } else if (key == "levelDb") {
    const float db = std::clamp(value, -60.0f, 12.0f);
    setCabLevel(db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f));
  }
  return true;
}

bool PedalEngine::addStereoWidener(std::string id, float sampleRate, std::string& error)
{
  return chain_.addStereoWidener(std::move(id), sampleRate, error);
}

bool PedalEngine::setStereoWidenerParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setStereoWidenerParameter(id, key, value)) return true;
  return chain_.setStereoWidenerParameter(id, key, value);
}

bool PedalEngine::addDaisyFx(std::string id, const std::string& blockType, const nlohmann::json& params,
                             float sampleRate, std::string& error, bool sceneLetRing)
{
  DaisyFxProcessor processor;
  if (!processor.configure(blockType, params, sampleRate, error)) {
    return false;
  }
  chain_.addDaisy(std::move(id), std::move(processor), sceneLetRing);
  return true;
}

bool PedalEngine::setDaisyParameter(const std::string& id, const std::string& key, float normalized)
{
  if (wdwRouting_ && wdwRouting_->setDaisyParameter(id, key, normalized)) return true;
  return chain_.setDaisyParameter(id, key, normalized);
}

bool PedalEngine::addCompressor(std::string id, const nlohmann::json& params, float sampleRate, std::string& error)
{
  CompressorProcessor processor;
  if (!processor.configure(params, sampleRate, error)) {
    return false;
  }
  chain_.addCompressor(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setCompressorParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setCompressorParameter(id, key, value)) return true;
  return chain_.setCompressorParameter(id, key, value);
}

float PedalEngine::compressorGainReductionDb(const std::string& id) const
{
  float value = 0.0f;
  if (wdwRouting_ && wdwRouting_->compressorGainReductionDb(id, value)) return value;
  chain_.compressorGainReductionDb(id, value);
  return value;
}

bool PedalEngine::addNoiseGate(std::string id, const nlohmann::json& params,
                               float sampleRate, std::string& error)
{
  NoiseGateProcessor processor;
  if (!processor.configure(params, sampleRate, error)) {
    return false;
  }
  chain_.addNoiseGate(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setNoiseGateParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setNoiseGateParameter(id, key, value)) return true;
  return chain_.setNoiseGateParameter(id, key, value);
}

bool PedalEngine::addTransientShaper(std::string id, const nlohmann::json& params,
                                     float sampleRate, std::string& error)
{
  TransientShaperProcessor processor;
  if (!processor.configure(params, sampleRate, error)) {
    return false;
  }
  chain_.addTransientShaper(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setTransientShaperParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setTransientShaperParameter(id, key, value)) return true;
  return chain_.setTransientShaperParameter(id, key, value);
}

bool PedalEngine::addDistortion(std::string id, const nlohmann::json& params,
                                float sampleRate, std::string& error)
{
  const auto mode = params.value("mode", std::string{"rat"});
  if (mode == "big_cheese") {
    CheeseProcessor processor;
    if (!processor.configure(params, sampleRate, error)) return false;
    chain_.addDistortion(std::move(id), std::move(processor));
    return true;
  }
  if (mode == "tape") {
    TapeProcessor processor;
    if (!processor.configure(params, sampleRate, error)) return false;
    chain_.addDistortion(std::move(id), std::move(processor));
    return true;
  }
  RatProcessor processor;
  if (!processor.configure(params, sampleRate, error)) return false;
  chain_.addDistortion(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setDistortionParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setDistortionParameter(id, key, value)) return true;
  return chain_.setDistortionParameter(id, key, value);
}

bool PedalEngine::addWah(std::string id, const nlohmann::json& params, float sampleRate,
                         const std::filesystem::path& tablePath, std::string& error)
{
  WahProcessor processor;
  if (!processor.configure(params, sampleRate, tablePath, error)) return false;
  chain_.addWah(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setWahParameter(const std::string& id, const std::string& key, float value)
{
  if (wdwRouting_ && wdwRouting_->setWahParameter(id, key, value)) return true;
  return chain_.setWahParameter(id, key, value);
}

bool PedalEngine::setBlockEnabled(const std::string& id, bool enabled)
{
  if (wdwRouting_ && wdwRouting_->setBlockEnabled(id, enabled)) return true;
  return chain_.setBlockEnabled(id, enabled);
}

bool PedalEngine::addParametricEq(const std::string& id, const nlohmann::json& params,
                                  float sampleRate, std::string& error)
{
  return chain_.addParametricEq(id, parametricEqParamsFromJson(params), sampleRate, error);
}

bool PedalEngine::setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params)
{
  if (wdwRouting_ && wdwRouting_->setParametricEqBand(id, band, params)) return true;
  return chain_.setParametricEqBand(id, band, params);
}

bool PedalEngine::setParametricEqPassFilter(const std::string& id, EqPassFilterKind kind,
                                            const EqPassFilterParams& params)
{
  if (wdwRouting_ && wdwRouting_->setParametricEqPassFilter(id, kind, params)) return true;
  return chain_.setParametricEqPassFilter(id, kind, params);
}

void PedalEngine::prepareBlockSize(size_t frames)
{
  if (frames == 0 || blockSize_ == frames) {
    return;
  }
  if (wdwRouting_ && wdwRouting_->blockSize() != frames) {
    return;
  }
  blockSize_ = frames;
  sanitizedInput_.assign(frames, 0.0f);
  gainedInput_.assign(frames, 0.0f);
  cabLevelBlock_.assign(frames, 1.0f);
  cabMixBlock_.assign(frames, 1.0f);
  sceneInputGainBlock_.assign(frames, 1.0f);
  sceneTrimBlock_.assign(frames, 1.0f);
  chain_.prepareBlockSize(frames);
}

void PedalEngine::clearEffects()
{
  sceneTransition_.reset();
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  flexibleRouting_.reset();
  wdwRouting_.reset();
  chain_.clear();
}

void PedalEngine::setInputGain(float gain)
{
  inputGain_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setOutputGain(float gain)
{
  outputGain_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setMasterVolume(float gain)
{
  masterVolume_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setEffectsBypassed(bool bypassed)
{
  // This setter is used by the control thread while process/processBlock run
  // on the audio thread. Resetting NAM, convolution, or time effects from the
  // callback is unbounded work, so bypass is deliberately state-preserving.
  effectsBypassed_.store(bypassed, std::memory_order_relaxed);
}

void PedalEngine::setSafetyLimit(float limit)
{
  safetyLimit_.store(std::isfinite(limit) ? std::max(0.0f, limit) : 0.8912509f, std::memory_order_relaxed);
}

void PedalEngine::setSafetyLimiterEnabled(bool enabled)
{
  safetyLimiterEnabled_.store(enabled, std::memory_order_relaxed);
}

void PedalEngine::setCabLevel(float gain)
{
  const float level = std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f;
  cabLevel_.store(level, std::memory_order_relaxed);
}

void PedalEngine::setCabMix(float mix)
{
  const float cabMix = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 1.0f;
  cabMix_.store(cabMix, std::memory_order_relaxed);
}

bool PedalEngine::prepareLooper(size_t memoryBudgetBytes, std::string& error)
{
  if (looperPrepared_.load(std::memory_order_acquire)) {
    error.clear();
    return true;
  }
  if (blockSize_ == 0) {
    error = "prepare the engine block size before preparing the looper";
    return false;
  }
  if (!looper_.prepare(sampleRate_, blockSize_, memoryBudgetBytes, error)) return false;
  looperPrepared_.store(true, std::memory_order_release);
  return true;
}

bool PedalEngine::looperPrepared() const noexcept
{
  return looperPrepared_.load(std::memory_order_acquire);
}

bool PedalEngine::looperSessionOpen() const noexcept
{
  return looperPrepared()
      && looper_.sessionOpen();
}

bool PedalEngine::tryEnqueueLooperCommand(const LooperCommand& command) noexcept
{
  return looperPrepared() && looper_.tryEnqueue(command);
}

bool PedalEngine::tryReadLooperTelemetry(LooperTelemetry& telemetry) noexcept
{
  return looperPrepared() && looper_.tryReadTelemetry(telemetry);
}

bool PedalEngine::installPreparedScenes(SceneTransitionProgram program, std::string& error)
{
  auto controller = std::make_unique<SceneTransitionController>();
  if (!controller->prepare(std::move(program))) {
    error = "scene transition program is invalid";
    return false;
  }
  sceneTransition_ = std::move(controller);
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  const auto& targets = sceneTransition_->targets();
  for (std::size_t index = 0; index < targets.size(); ++index) {
    if (targets[index].address.kind == SceneRuntimeTargetKind::InputGainDb) {
      sceneInputGainTarget_ = index;
      break;
    }
  }
  if (!applySceneValues()) {
    sceneTransition_.reset();
    error = "scene transition target could not be resolved in the prepared DSP program";
    return false;
  }
  error.clear();
  return true;
}

bool PedalEngine::tryRequestScene(const SceneTransitionRequest& request) noexcept
{
  return sceneTransition_ && sceneTransition_->request(request);
}

bool PedalEngine::tryOverrideSceneTarget(std::size_t targetIndex, float value) noexcept
{
  return sceneTransition_ && sceneTransition_->requestOverride(targetIndex, value);
}

void PedalEngine::requestSceneValueSnapshot() noexcept
{
  sceneSnapshotRequested_.store(true, std::memory_order_release);
}

bool PedalEngine::tryReadSceneValueSnapshot(std::uint64_t& lastSerial,
                                            std::vector<float>& values) const
{
  const auto serial = sceneSnapshotSerial_.load(std::memory_order_acquire);
  if (serial == lastSerial) return false;
  const auto count = std::min(sceneSnapshotCount_.load(std::memory_order_acquire),
                              SceneTransitionController::kMaximumTargets);
  values.resize(count);
  for (std::size_t index = 0; index < count; ++index)
    values[index] = sceneSnapshotValues_[index].load(std::memory_order_relaxed);
  lastSerial = serial;
  return true;
}

SceneTransitionTelemetry PedalEngine::sceneTransitionTelemetry() const noexcept
{
  return sceneTransition_ ? sceneTransition_->telemetry() : SceneTransitionTelemetry{};
}

std::uint64_t PedalEngine::scenePresetGeneration() const noexcept
{
  return sceneTransition_ ? sceneTransition_->presetGeneration() : 0;
}

bool PedalEngine::applySceneValues() noexcept
{
  if (!sceneTransition_) return true;
  const auto& targets = sceneTransition_->targets();
  const auto values = sceneTransition_->currentValues();
  if (sceneSnapshotRequested_.exchange(false, std::memory_order_acq_rel)) {
    const auto count = std::min(values.size(), SceneTransitionController::kMaximumTargets);
    for (std::size_t index = 0; index < count; ++index)
      sceneSnapshotValues_[index].store(values[index], std::memory_order_relaxed);
    sceneSnapshotCount_.store(count, std::memory_order_release);
    sceneSnapshotSerial_.fetch_add(1, std::memory_order_release);
  }
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const auto& address = targets[index].address;
    if (address.kind == SceneRuntimeTargetKind::InputGainDb) continue;
    bool applied = false;
    if (wdwRouting_) applied = wdwRouting_->applySceneTarget(address, values[index]);
    else if (address.kind != SceneRuntimeTargetKind::WdwLaneParameter)
      applied = chain_.applySceneTarget(address, values[index]);
    if (!applied) return false;
    if (!wdwRouting_ && address.kind == SceneRuntimeTargetKind::CabParameter
        && !address.child && address.container == SceneBlockContainer::Serial) {
      const auto parameter = static_cast<SceneRuntimeParameter>(address.parameterIndex);
      if (parameter == SceneRuntimeParameter::Mix) setCabMix(values[index]);
      else if (parameter == SceneRuntimeParameter::LevelDb) {
        const float db = std::clamp(values[index], -60.0f, 12.0f);
        setCabLevel(db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f));
      }
    }
  }
  return true;
}

bool PedalEngine::restorePausedLooperSession(const LooperPausedSessionView& session,
                                             std::string& error)
{
  if (!looperPrepared()) {
    error = "prepare looper memory before restoring a session";
    return false;
  }
  return looper_.restorePausedSession(session, error);
}

std::optional<LooperPausedSessionView> PedalEngine::pausedLooperSessionView() const noexcept
{
  return looperPrepared() ? looper_.pausedSessionView() : std::nullopt;
}

uint64_t PedalEngine::nonFiniteInputSamples() const noexcept
{
  return nonFiniteInputSamples_.load(std::memory_order_relaxed);
}

uint64_t PedalEngine::blockSizeMismatchCount() const noexcept
{
  return blockSizeMismatchCount_.load(std::memory_order_relaxed);
}

uint64_t PedalEngine::nonFiniteBlockCount() const noexcept
{
  if (wdwRouting_) return wdwRouting_->nonFiniteBlockCount();
  if (flexibleRouting_) return flexibleRouting_->nonFiniteBlockCount();
  return chain_.nonFiniteBlockCount();
}

uint64_t PedalEngine::parallelWaitOverBudgetCount() const noexcept
{
  if (wdwRouting_) return wdwRouting_->parallelWaitOverBudgetCount();
  if (flexibleRouting_) return flexibleRouting_->parallelWaitOverBudgetCount();
  return chain_.parallelWaitOverBudgetCount();
}

uint64_t PedalEngine::parallelUnderflowCount() const noexcept
{
  return wdwRouting_ ? wdwRouting_->pairUnderflowCount() : 0;
}

uint64_t PedalEngine::parallelSubmissionMissCount() const noexcept
{
  return wdwRouting_ ? wdwRouting_->pairSubmissionMissCount() : 0;
}

bool PedalEngine::parallelWorkersReady() const noexcept
{
  return !wdwRouting_ || wdwRouting_->workersReady();
}

std::string PedalEngine::firstNonFiniteBlockId() const
{
  if (wdwRouting_) return wdwRouting_->firstNonFiniteBlockId();
  if (flexibleRouting_) return flexibleRouting_->firstNonFiniteBlockId();
  return chain_.firstNonFiniteBlockId();
}

ClipDiagnosticsSnapshot PedalEngine::takeClipDiagnostics()
{
  ClipDiagnosticsSnapshot diagnostics;
  std::vector<ClipStageSnapshot> chainDiagnostics;
  if (wdwRouting_) {
    auto routingDiagnostics = wdwRouting_->takeClipDiagnostics();
    chainDiagnostics = std::move(routingDiagnostics.stages);
  } else if (flexibleRouting_) {
    auto routingDiagnostics = flexibleRouting_->takeClipDiagnostics();
    chainDiagnostics = std::move(routingDiagnostics.stages);
  } else {
    chainDiagnostics = chain_.takeClipDiagnostics();
  }
  diagnostics.stages.reserve(chainDiagnostics.size() + 2);
  diagnostics.stages.push_back(takeLevel(inputPeakBits_, inputOverloadFrames_,
                                         SignalStageKind::Input, {}));
  for (auto& stage : chainDiagnostics) {
    diagnostics.stages.push_back(std::move(stage));
  }
  diagnostics.stages.push_back(takeLevel(outputPeakBits_, outputOverloadFrames_,
                                         SignalStageKind::Output, {}));
  diagnostics.limiterFrames = limiterFrames_.exchange(0, std::memory_order_relaxed);
  return diagnostics;
}

void PedalEngine::replacePreparedProgram(PedalEngine&& prepared)
{
  chain_ = std::move(prepared.chain_);
  flexibleRouting_ = std::move(prepared.flexibleRouting_);
  wdwRouting_ = std::move(prepared.wdwRouting_);
  blockSize_ = prepared.blockSize_;
  sanitizedInput_ = std::move(prepared.sanitizedInput_);
  gainedInput_ = std::move(prepared.gainedInput_);
  cabLevelBlock_ = std::move(prepared.cabLevelBlock_);
  cabMixBlock_ = std::move(prepared.cabMixBlock_);
  sceneInputGainBlock_ = std::move(prepared.sceneInputGainBlock_);
  sceneTrimBlock_ = std::move(prepared.sceneTrimBlock_);
  sceneTransition_ = std::move(prepared.sceneTransition_);
  sceneInputGainTarget_ = prepared.sceneInputGainTarget_;
  sampleRate_ = prepared.sampleRate_;
  gainSmoothingCoefficient_ = prepared.gainSmoothingCoefficient_;

  inputGain_.store(prepared.inputGain_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  outputGain_.store(prepared.outputGain_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  safetyLimit_.store(prepared.safetyLimit_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  cabLevel_.store(prepared.cabLevel_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  cabMix_.store(prepared.cabMix_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  safetyLimiterEnabled_.store(prepared.safetyLimiterEnabled_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);

  inputPeakBits_.store(0, std::memory_order_relaxed);
  inputOverloadFrames_.store(0, std::memory_order_relaxed);
  outputPeakBits_.store(0, std::memory_order_relaxed);
  outputOverloadFrames_.store(0, std::memory_order_relaxed);
  limiterFrames_.store(0, std::memory_order_relaxed);

  // Master volume and bypass belong to the live host rather than a preset.
  // Keep their targets and current ramp positions so a successful swap cannot
  // unexpectedly change hardware volume or introduce a gain step.
}

bool PedalEngine::installPreparedRouting(
  std::unique_ptr<FlexibleRoutingProgram> program, std::string& error)
{
  error.clear();
  if (!program || !program->prepared()) {
    error = "flexible routing installation requires a prepared program";
    return false;
  }
  if (program->blockSize() == 0) {
    error = "flexible routing installation requires a non-zero block size";
    return false;
  }
  if (blockSize_ == 0) {
    prepareBlockSize(program->blockSize());
  }
  if (blockSize_ != program->blockSize()) {
    error = "flexible routing block size does not match the engine quantum";
    return false;
  }

  // The two program types are mutually exclusive. Destroy the old worker
  // graph before clearing the legacy chain, then publish the new immutable
  // owner while audio is stopped as required by the API contract.
  flexibleRouting_.reset();
  wdwRouting_.reset();
  sceneTransition_.reset();
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  chain_.clear();
  flexibleRouting_ = std::move(program);
  return true;
}

bool PedalEngine::flexibleRoutingEnabled() const noexcept
{
  return static_cast<bool>(flexibleRouting_);
}

void PedalEngine::clearPreparedRouting()
{
  sceneTransition_.reset();
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  flexibleRouting_.reset();
  wdwRouting_.reset();
}

bool PedalEngine::installPreparedWdwRouting(
  std::unique_ptr<WdwRoutingProgram> program, std::string& error)
{
  error.clear();
  if (!program || !program->prepared()) {
    error = "WDW routing installation requires a prepared program";
    return false;
  }
  if (program->blockSize() == 0) {
    error = "WDW routing installation requires a non-zero block size";
    return false;
  }
  if (blockSize_ == 0) {
    prepareBlockSize(program->blockSize());
  }
  if (blockSize_ != program->blockSize()) {
    error = "WDW routing block size does not match the engine quantum";
    return false;
  }

  flexibleRouting_.reset();
  wdwRouting_.reset();
  sceneTransition_.reset();
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  chain_.clear();
  wdwRouting_ = std::move(program);
  return true;
}

bool PedalEngine::wdwRoutingEnabled() const noexcept
{
  return static_cast<bool>(wdwRouting_);
}

void PedalEngine::clearPreparedWdwRouting()
{
  sceneTransition_.reset();
  sceneInputGainTarget_ = static_cast<std::size_t>(-1);
  wdwRouting_.reset();
}

void PedalEngine::reset()
{
  if (wdwRouting_) wdwRouting_->reset();
  else if (flexibleRouting_) flexibleRouting_->reset();
  else chain_.reset();
}

size_t PedalEngine::tailFrames() const noexcept
{
  if (wdwRouting_) return wdwRouting_->tailFrames();
  if (flexibleRouting_) return flexibleRouting_->tailFrames();
  return chain_.tailFrames();
}

void PedalEngine::beginAudioProcessing()
{
  if (audioStarted_) {
    return;
  }
  currentInputGain_ = inputGain_.load(std::memory_order_relaxed);
  currentOutputGain_ = outputGain_.load(std::memory_order_relaxed);
  currentMasterVolume_ = masterVolume_.load(std::memory_order_relaxed);
  currentCabLevel_ = cabLevel_.load(std::memory_order_relaxed);
  currentCabMix_ = cabMix_.load(std::memory_order_relaxed);
  currentEffectsMix_ = effectsBypassed_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  audioStarted_ = true;
}

float PedalEngine::smoothGain(float& current, float target) const
{
  current += (target - current) * gainSmoothingCoefficient_;
  return current;
}

float PedalEngine::smoothEffectsMix(float target)
{
  currentEffectsMix_ += (target - currentEffectsMix_) * gainSmoothingCoefficient_;
  if (std::fabs(target - currentEffectsMix_) < 1.0e-4f) {
    currentEffectsMix_ = target;
  }
  return currentEffectsMix_;
}

StereoSample PedalEngine::bypassMix(StereoSample dry, StereoSample wet, float wetMix)
{
  const float clampedMix = std::clamp(wetMix, 0.0f, 1.0f);
  if (clampedMix <= 0.0f) {
    return dry;
  }
  if (clampedMix >= 1.0f) {
    return wet;
  }
  // The dry and processed signals are normally strongly correlated. A linear
  // crossfade therefore preserves unity for transparent chains, while an
  // equal-power curve would add 3 dB halfway through every bypass transition.
  const float dryGain = 1.0f - clampedMix;
  return {dry.left * dryGain + wet.left * clampedMix,
          dry.right * dryGain + wet.right * clampedMix};
}

std::pair<float, float> PedalEngine::process(float input)
{
  const ScopedDenormalGuard denormalGuard;
  beginAudioProcessing();
  if (sceneTransition_) {
    sceneTransition_->beginBlock();
    applySceneValues();
  }
  if (!std::isfinite(input)) {
    nonFiniteInputSamples_.fetch_add(1, std::memory_order_relaxed);
    input = 0.0f;
  }
  const float inputGain = inputGain_.load(std::memory_order_relaxed);
  const float outputGain = outputGain_.load(std::memory_order_relaxed);
  const float masterVolumeTarget = masterVolume_.load(std::memory_order_relaxed);
  const float cabLevel = cabLevel_.load(std::memory_order_relaxed);
  const float cabMix = cabMix_.load(std::memory_order_relaxed);
  const float effectsMix = effectsBypassed_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  const bool limiterEnabled = safetyLimiterEnabled_.load(std::memory_order_relaxed);
  const float safetyLimit = safetyLimit_.load(std::memory_order_relaxed);
  const float sceneInputGain = sceneTransition_ && sceneInputGainTarget_ < sceneTransition_->currentValues().size()
    ? std::pow(10.0f, sceneTransition_->currentValues()[sceneInputGainTarget_] / 20.0f)
    : smoothGain(currentInputGain_, inputGain);
  const float afterGain = input * sceneInputGain;
  observeLevel(inputPeakBits_, inputOverloadFrames_, afterGain, afterGain);
  const float smoothedEffectsMix = smoothEffectsMix(effectsMix);
  StereoSample wet{};
  if (smoothedEffectsMix <= 0.0f) {
    wet = {input, input};
  } else if (wdwRouting_) {
    // The WDW program owns the dry/wet lane mix. The host-level effects
    // bypass control still selects raw input when explicitly enabled.
    if (!wdwRouting_->processSample(afterGain, wet.left, wet.right)) {
      throw std::logic_error(
        "scalar processing is unavailable for a block-quantized WDW program");
    }
  } else if (flexibleRouting_) {
    float wetLeft = 0.0f;
    float wetRight = 0.0f;
    if (flexibleRouting_->processSample(afterGain, wetLeft, wetRight)) {
      wet = {wetLeft, wetRight};
    }
  } else {
    wet = chain_.process({afterGain, afterGain}, smoothGain(currentCabLevel_, cabLevel),
                         smoothGain(currentCabMix_, cabMix));
  }
  const float output = smoothGain(currentOutputGain_, outputGain);
  StereoSample mixed = bypassMix({input, input}, {wet.left * output, wet.right * output},
                                 smoothedEffectsMix);
  if (sceneTransition_) {
    const float trim = std::pow(10.0f, sceneTransition_->currentOutputTrimDb() / 20.0f);
    mixed.left *= trim;
    mixed.right *= trim;
    sceneTransition_->advanceFrame();
  }
  if (looperPrepared()) {
    looper_.processBlock(&mixed.left, &mixed.right, 1);
  }
  const float masterVolume = smoothGain(currentMasterVolume_, masterVolumeTarget);
  mixed.left *= masterVolume;
  mixed.right *= masterVolume;
  observeLevel(outputPeakBits_, outputOverloadFrames_, mixed.left, mixed.right);
  if (limiterEngaged(mixed.left, mixed.right, limiterEnabled, safetyLimit)) {
    limiterFrames_.fetch_add(1, std::memory_order_relaxed);
  }
  return {applySafety(mixed.left, limiterEnabled, safetyLimit),
          applySafety(mixed.right, limiterEnabled, safetyLimit)};
}

void PedalEngine::processBlock(const float* input, float* left, float* right, size_t frames)
{
  const ScopedDenormalGuard denormalGuard;
  beginAudioProcessing();
  if (blockSize_ == 0) {
    prepareBlockSize(frames);
  }
  if (frames != blockSize_) {
    // The realtime adapter owns fixed-quantum assembly. Processing a partial
    // remainder would force a prepared convolver into a different algorithm
    // and corrupt its state, so fail boundedly and make the fault observable.
    blockSizeMismatchCount_.fetch_add(1, std::memory_order_relaxed);
    std::fill(left, left + frames, 0.0f);
    std::fill(right, right + frames, 0.0f);
    return;
  }

  if (sceneTransition_) {
    sceneTransition_->beginBlock();
    applySceneValues();
  }

  const float inputGain = inputGain_.load(std::memory_order_relaxed);
  const float outputGain = outputGain_.load(std::memory_order_relaxed);
  const float masterVolume = masterVolume_.load(std::memory_order_relaxed);
  const float cabLevel = cabLevel_.load(std::memory_order_relaxed);
  const float cabMix = cabMix_.load(std::memory_order_relaxed);
  const float effectsMix = effectsBypassed_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  const bool limiterEnabled = safetyLimiterEnabled_.load(std::memory_order_relaxed);
  const float safetyLimit = safetyLimit_.load(std::memory_order_relaxed);
  uint64_t nonFiniteSamples = 0;
  for (size_t i = 0; i < frames; ++i) {
    const bool finite = std::isfinite(input[i]);
    const float safeInput = finite ? input[i] : 0.0f;
    if (!finite) {
      ++nonFiniteSamples;
    }
    sanitizedInput_[i] = safeInput;
    if (sceneTransition_) {
      const auto values = sceneTransition_->currentValues();
      sceneInputGainBlock_[i] = sceneInputGainTarget_ < values.size()
        ? std::pow(10.0f, values[sceneInputGainTarget_] / 20.0f)
        : smoothGain(currentInputGain_, inputGain);
      sceneTrimBlock_[i] = std::pow(10.0f, sceneTransition_->currentOutputTrimDb() / 20.0f);
      sceneTransition_->advanceFrame();
    } else {
      sceneInputGainBlock_[i] = smoothGain(currentInputGain_, inputGain);
      sceneTrimBlock_[i] = 1.0f;
    }
    gainedInput_[i] = safeInput * sceneInputGainBlock_[i];
    cabLevelBlock_[i] = smoothGain(currentCabLevel_, cabLevel);
    cabMixBlock_[i] = smoothGain(currentCabMix_, cabMix);
  }
  if (nonFiniteSamples > 0) {
    nonFiniteInputSamples_.fetch_add(nonFiniteSamples, std::memory_order_relaxed);
  }
  float inputPeak = 0.0f;
  uint64_t inputOverloads = 0;
  for (size_t i = 0; i < frames; ++i) {
    const float magnitude = std::fabs(gainedInput_[i]);
    inputPeak = std::max(inputPeak, magnitude);
    inputOverloads += magnitude > 1.0f ? 1U : 0U;
  }
  commitLevel(inputPeakBits_, inputOverloadFrames_, inputPeak, inputOverloads);
  // Once a bypass fade has reached dry, stop executing the chain. This makes
  // the overload latch an actual CPU escape hatch instead of only changing
  // what is audible while the expensive processors continue to run.
  const bool processEffects = currentEffectsMix_ > 0.0f || effectsMix > 0.0f;
  if (processEffects) {
    bool routingProcessed = false;
    if (wdwRouting_) {
      WdwRoutingProcessResult routingResult;
      routingProcessed = wdwRouting_->processBlock(gainedInput_.data(), left, right,
                                                    frames, routingResult);
    } else if (flexibleRouting_) {
      FlexibleRoutingGraphProcessResult routingResult;
      routingProcessed = flexibleRouting_->processBlock(gainedInput_.data(), left, right,
                                                         frames, routingResult);
    } else {
      chain_.processBlock(gainedInput_.data(), left, right, frames,
                          cabLevelBlock_.data(), cabMixBlock_.data());
      routingProcessed = true;
    }
    if (!routingProcessed) {
      std::fill(left, left + frames, 0.0f);
      std::fill(right, right + frames, 0.0f);
    }
  } else {
    std::copy(sanitizedInput_.begin(), sanitizedInput_.begin() + static_cast<std::ptrdiff_t>(frames), left);
    std::copy(sanitizedInput_.begin(), sanitizedInput_.begin() + static_cast<std::ptrdiff_t>(frames), right);
  }
  // Build the complete preset program first. The host looper captures this
  // post-output-gain signal, but deliberately remains before master volume and
  // the safety limiter so changing stage volume never alters stored audio.
  for (size_t i = 0; i < frames; ++i) {
    const float output = smoothGain(currentOutputGain_, outputGain);
    const StereoSample mixed = bypassMix({sanitizedInput_[i], sanitizedInput_[i]},
                                         {left[i] * output, right[i] * output},
                                         smoothEffectsMix(effectsMix));
    left[i] = mixed.left * sceneTrimBlock_[i];
    right[i] = mixed.right * sceneTrimBlock_[i];
  }
  if (looperPrepared()) {
    looper_.processBlock(left, right, frames);
  }

  float outputPeak = 0.0f;
  uint64_t outputOverloads = 0;
  uint64_t limitedFrames = 0;
  for (size_t i = 0; i < frames; ++i) {
    const float master = smoothGain(currentMasterVolume_, masterVolume);
    const float mixedLeft = left[i] * master;
    const float mixedRight = right[i] * master;
    const float framePeak = std::max(std::fabs(mixedLeft), std::fabs(mixedRight));
    outputPeak = std::max(outputPeak, framePeak);
    outputOverloads += framePeak > 1.0f ? 1U : 0U;
    limitedFrames += limiterEngaged(mixedLeft, mixedRight, limiterEnabled, safetyLimit) ? 1U : 0U;
    left[i] = applySafety(mixedLeft, limiterEnabled, safetyLimit);
    right[i] = applySafety(mixedRight, limiterEnabled, safetyLimit);
  }
  commitLevel(outputPeakBits_, outputOverloadFrames_, outputPeak, outputOverloads);
  if (limitedFrames > 0) {
    limiterFrames_.fetch_add(limitedFrames, std::memory_order_relaxed);
  }
}

bool PedalEngine::limiterEngaged(float left, float right, bool enabled, float limit)
{
  constexpr float kKneeFraction = 0.95f;
  return enabled && limit > 0.0f
    && std::max(std::fabs(left), std::fabs(right)) > limit * kKneeFraction;
}

float PedalEngine::applySafety(float sample, bool limiterEnabled, float safetyLimit)
{
  if (!std::isfinite(sample)) {
    return 0.0f;
  }
  if (!limiterEnabled) {
    return sample;
  }

  if (safetyLimit <= 0.0f) {
    return sample;
  }

  // The final stage is a safeguard for the DAC, not an effect.  A hard clamp
  // here creates an unpleasant digital edge whenever a NAM/IR combination has
  // a transient above the preset ceiling.  Keep normal program material
  // bit-for-bit unchanged, then use a rational soft knee that is continuous
  // in both value and slope and asymptotically approaches the ceiling.  This
  // costs one divide only for an actual overload and needs no look-ahead,
  // allocation, or per-preset trim.
  constexpr float kKneeFraction = 0.95f;
  const float magnitude = std::fabs(sample);
  const float kneeStart = safetyLimit * kKneeFraction;
  if (magnitude <= kneeStart) {
    return sample;
  }

  const float kneeWidth = safetyLimit - kneeStart;
  const float normalizedExcess = (magnitude - kneeStart) / kneeWidth;
  const float limitedMagnitude = kneeStart + kneeWidth * normalizedExcess / (1.0f + normalizedExcess);
  return std::copysign(limitedMagnitude, sample);
}

} // namespace ardor
