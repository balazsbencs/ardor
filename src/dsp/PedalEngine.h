#pragma once

#include "RuntimeChain.h"

#include <atomic>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  bool loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  void loadIr(std::vector<float> impulse);
  void addCab(std::vector<float> impulse, float level, float mix);
  bool addDaisyFx(const std::string& blockType, const nlohmann::json& params, float sampleRate, std::string& error);
  void prepareBlockSize(size_t frames);
  void clearEffects();
  void setInputGain(float gain);
  void setOutputGain(float gain);
  void setMasterVolume(float gain);
  void setEffectsBypassed(bool bypassed);
  void setSafetyLimit(float limit);
  void setSafetyLimiterEnabled(bool enabled);
  void setCabLevel(float gain);
  void setCabMix(float mix);
  std::pair<float, float> process(float input);
  void processBlock(const float* input, float* left, float* right, size_t frames);
  void reset();

private:
  std::atomic<float> inputGain_{1.0f};
  std::atomic<float> outputGain_{1.0f};
  std::atomic<float> masterVolume_{1.0f};
  std::atomic<float> safetyLimit_{0.8912509f};
  std::atomic<float> cabLevel_{1.0f};
  std::atomic<float> cabMix_{1.0f};
  std::atomic<bool> effectsBypassed_{false};
  std::atomic<bool> safetyLimiterEnabled_{true};
  std::atomic<bool> resetRequested_{false};
  RuntimeChain chain_;
  size_t blockSize_ = 0;

  void applyPendingReset();
  float applySafety(float sample) const;
};

} // namespace ardor
