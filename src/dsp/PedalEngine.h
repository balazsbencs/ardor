#pragma once

#include "ClipDiagnostics.h"
#include "RuntimeChain.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  void setSampleRate(double sampleRate);
  bool loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
               std::string id = "nam", float slimmableSize = 1.0f);
  void loadIr(std::vector<float> impulse);
  void addCab(std::vector<float> impulse, float level, float mix, std::string id = "cab");
  bool addDaisyFx(std::string id, const std::string& blockType, const nlohmann::json& params,
                  float sampleRate, std::string& error);
  bool addCompressor(std::string id, const nlohmann::json& params, float sampleRate, std::string& error);
  bool addParametricEq(const std::string& id, const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
  bool setDaisyParameter(const std::string& id, const std::string& key, float normalized);
  bool setCompressorParameter(const std::string& id, const std::string& key, float value);
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
  uint64_t nonFiniteInputSamples() const noexcept;
  uint64_t blockSizeMismatchCount() const noexcept;
  uint64_t nonFiniteBlockCount() const noexcept;
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
  std::atomic<uint64_t> nonFiniteInputSamples_{0};
  std::atomic<uint64_t> blockSizeMismatchCount_{0};
  std::atomic<uint32_t> inputPeakBits_{0};
  std::atomic<uint64_t> inputOverloadFrames_{0};
  std::atomic<uint32_t> outputPeakBits_{0};
  std::atomic<uint64_t> outputOverloadFrames_{0};
  std::atomic<uint64_t> limiterFrames_{0};
  RuntimeChain chain_;
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
  float smoothGain(float& current, const std::atomic<float>& target);
  float smoothEffectsMix();
  static StereoSample equalPowerMix(StereoSample dry, StereoSample wet, float wetMix);
  void observeLimiter(float left, float right);
  float applySafety(float sample) const;
};

} // namespace ardor
