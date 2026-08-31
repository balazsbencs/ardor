#pragma once

#include "ClipDiagnostics.h"
#include "RuntimeChain.h"
#include "looper/RealtimeLooper.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  void setSampleRate(double sampleRate);
  bool loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
               std::string id = "nam", float slimmableSize = 1.0f,
               NamInputMode inputMode = NamInputMode::Sum);
  bool addDualAmp(std::string id, DualAmpLaneConfig left, DualAmpLaneConfig right,
                  NamInputMode inputMode, double sampleRate, int maxBlockSize,
                  bool requestParallel, int workerCpu, std::string& error);
  bool addDualRig(std::string id, DualRigLaneConfig left, DualRigLaneConfig right,
                  NamInputMode inputMode, double sampleRate, int maxBlockSize,
                  bool requestParallel, int workerCpu, std::string& error);
  void loadIr(std::vector<float> impulse);
  void addCab(std::vector<float> impulse, float level, float mix, std::string id = "cab");
  bool addIrReverb(std::string id, std::vector<float> left, std::vector<float> right,
                   float sampleRate, std::string& error);
  bool setIrReverbParameter(const std::string& id, const std::string& key, float value);
  bool addStereoWidener(std::string id, float sampleRate, std::string& error);
  bool setStereoWidenerParameter(const std::string& id, const std::string& key, float value);
  bool addDaisyFx(std::string id, const std::string& blockType, const nlohmann::json& params,
                  float sampleRate, std::string& error);
  bool addCompressor(std::string id, const nlohmann::json& params, float sampleRate, std::string& error);
  bool addNoiseGate(std::string id, const nlohmann::json& params, float sampleRate, std::string& error);
  bool addTransientShaper(std::string id, const nlohmann::json& params, float sampleRate, std::string& error);
  // Dispatches on the mode: the RAT and the Big Cheese are both distortion
  // blocks and share the parameter path below.
  bool addDistortion(std::string id, const nlohmann::json& params, float sampleRate,
                     std::string& error);
  bool setDistortionParameter(const std::string& id, const std::string& key, float value);
  bool addWah(std::string id, const nlohmann::json& params, float sampleRate,
              const std::filesystem::path& tablePath, std::string& error);
  bool addParametricEq(const std::string& id, const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
  bool setParametricEqPassFilter(const std::string& id, EqPassFilterKind kind,
                                 const EqPassFilterParams& params);
  bool setDaisyParameter(const std::string& id, const std::string& key, float normalized);
  bool setCompressorParameter(const std::string& id, const std::string& key, float value);
  // 0.0 if `id` does not name a live compressor block.
  float compressorGainReductionDb(const std::string& id) const;
  bool setNoiseGateParameter(const std::string& id, const std::string& key, float value);
  bool setTransientShaperParameter(const std::string& id, const std::string& key, float value);
  bool setWahParameter(const std::string& id, const std::string& key, float value);
  bool setBlockEnabled(const std::string& id, bool enabled);
  void prepareBlockSize(size_t frames);
  void clearEffects();
  void setInputGain(float gain);
  void setOutputGain(float gain);
  void setMasterVolume(float gain);
  void setEffectsBypassed(bool bypassed);
  // Sets the asymptotic ceiling of the transparent, zero-allocation final
  // soft limiter. It protects every chain from DAC clipping.
  void setSafetyLimit(float limit);
  void setSafetyLimiterEnabled(bool enabled);
  void setCabLevel(float gain);
  void setCabMix(float mix);
  // Allocates the host-level loop memory on the control thread. This must be
  // called after prepareBlockSize() and before looper commands are submitted.
  bool prepareLooper(size_t memoryBudgetBytes, std::string& error);
  bool looperPrepared() const noexcept;
  bool looperSessionOpen() const noexcept;
  bool tryEnqueueLooperCommand(const LooperCommand& command) noexcept;
  bool tryReadLooperTelemetry(LooperTelemetry& telemetry) noexcept;
  bool restorePausedLooperSession(const LooperPausedSessionView& session,
                                  std::string& error);
  std::optional<LooperPausedSessionView> pausedLooperSessionView() const noexcept;
  uint64_t nonFiniteInputSamples() const noexcept;
  uint64_t blockSizeMismatchCount() const noexcept;
  uint64_t nonFiniteBlockCount() const noexcept;
  uint64_t parallelWaitOverBudgetCount() const noexcept;
  std::string firstNonFiniteBlockId() const;
  // Consumes interval peaks/overloads for the post-input-gain boundary, every
  // chain block, and the final pre-limiter output boundary.
  ClipDiagnosticsSnapshot takeClipDiagnostics();
  // Exchanges a fully prepared program. This is a control-thread operation;
  // the caller must stop audio processing before invoking it.
  void replacePreparedProgram(PedalEngine&& prepared);
  std::pair<float, float> process(float input);
  void processBlock(const float* input, float* left, float* right, size_t frames);
  void reset();
  size_t tailFrames() const noexcept;

private:
  std::atomic<float> inputGain_{1.0f};
  std::atomic<float> outputGain_{1.0f};
  std::atomic<float> masterVolume_{1.0f};
  std::atomic<float> safetyLimit_{0.8912509f};
  std::atomic<float> cabLevel_{1.0f};
  std::atomic<float> cabMix_{1.0f};
  std::atomic<bool> effectsBypassed_{false};
  std::atomic<bool> safetyLimiterEnabled_{true};
  std::atomic<bool> looperPrepared_{false};
  std::atomic<uint64_t> nonFiniteInputSamples_{0};
  std::atomic<uint64_t> blockSizeMismatchCount_{0};
  std::atomic<uint32_t> inputPeakBits_{0};
  std::atomic<uint64_t> inputOverloadFrames_{0};
  std::atomic<uint32_t> outputPeakBits_{0};
  std::atomic<uint64_t> outputOverloadFrames_{0};
  std::atomic<uint64_t> limiterFrames_{0};
  RuntimeChain chain_;
  RealtimeLooper looper_;
  size_t blockSize_ = 0;
  std::vector<float> sanitizedInput_;
  std::vector<float> gainedInput_;
  std::vector<float> cabLevelBlock_;
  std::vector<float> cabMixBlock_;
  float sampleRate_ = 48000.0f;
  float gainSmoothingCoefficient_ = 0.004157998f; // 5 ms at 48 kHz.
  float currentInputGain_ = 1.0f;
  float currentOutputGain_ = 1.0f;
  float currentMasterVolume_ = 1.0f;
  float currentCabLevel_ = 1.0f;
  float currentCabMix_ = 1.0f;
  float currentEffectsMix_ = 1.0f;
  bool audioStarted_ = false;

  void beginAudioProcessing();
  float smoothGain(float& current, float target) const;
  float smoothEffectsMix(float target);
  static StereoSample equalPowerMix(StereoSample dry, StereoSample wet, float wetMix);
  static bool limiterEngaged(float left, float right, bool enabled, float limit);
  static float applySafety(float sample, bool limiterEnabled, float safetyLimit);
};

} // namespace ardor
