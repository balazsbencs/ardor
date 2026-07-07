#pragma once

#include "IrConvolver.h"
#include "NamProcessor.h"

#include <atomic>
#include <filesystem>
#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  bool loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  void loadIr(std::vector<float> impulse);
  void setInputGain(float gain);
  void setOutputGain(float gain);
  void setMasterVolume(float gain);
  void setEffectsBypassed(bool bypassed);
  void setSafetyLimit(float limit);
  void setSafetyLimiterEnabled(bool enabled);
  std::pair<float, float> process(float input);
  void processBlock(const float* input, float* left, float* right, size_t frames);
  void reset();

private:
  std::atomic<float> inputGain_{1.0f};
  std::atomic<float> outputGain_{1.0f};
  std::atomic<float> masterVolume_{1.0f};
  std::atomic<float> safetyLimit_{0.8912509f};
  std::atomic<bool> effectsBypassed_{false};
  std::atomic<bool> safetyLimiterEnabled_{true};
  std::atomic<bool> resetRequested_{false};
  NamProcessor nam_;
  IrConvolver ir_;
  std::vector<float> namBlock_;
  std::vector<float> irBlock_;

  void applyPendingReset();
  float applySafety(float sample) const;
};

} // namespace ardor
