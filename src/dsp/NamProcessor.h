#pragma once

#include "json.hpp"

#include <filesystem>
#include <memory>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace nam {
class DSP;
}

namespace ardor {

class NamProcessor {
public:
  NamProcessor();
  ~NamProcessor();

  // `slimmableSize` selects a SlimmableContainer tier before it is prewarmed.
  // 0 is the nano tier and 1 is the full tier; non-slim models ignore it.
  bool load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
            float slimmableSize = 1.0f,
            std::optional<float> inputReferenceLevelDbU = std::nullopt,
              const nlohmann::json& modelParameters = nlohmann::json::object());
  // Prepared documents let desktop hosts embed models in their project state.
  // Parsing/preparation remain control-thread operations, never audio callbacks.
  bool loadDocument(const nlohmann::json& document, double sampleRate, int maxBlockSize,
                    float slimmableSize = 1.0f,
                    std::optional<float> inputReferenceLevelDbU = std::nullopt,
                    const nlohmann::json& modelParameters = nlohmann::json::object());
  float process(float input);
  void processBlock(const float* input, float* output, size_t frames);
  void clear();
  void reset();
  bool loaded() const;
  // Control-thread mailbox. The audio thread commits targets between blocks;
  // the model's conditioner smooths continuous controls without resetting history.
  // May overlap processing, but not load(), clear(), or destruction.
  bool setParameterTarget(std::string_view name, float value);

  // Returns tier breakpoints if the model is a SlimmableContainer; empty otherwise.
  std::vector<double> slimmableSizeBreakpoints() const;
  // No-op if model is not slimmable.
  void setSlimmableSize(double val);
  float slimmableSize() const noexcept { return slimmableSize_; }
  std::optional<float> modelInputLevelDbU() const noexcept { return modelInputLevelDbU_; }
  float inputCalibrationGain() const noexcept { return inputCalibrationGain_; }

private:
  struct LiveParameters;
  void refreshLiveParameters();
  std::unique_ptr<LiveParameters> liveParameters_;
  std::unique_ptr<nam::DSP> model_;
  std::vector<float> input_{0.0f};
  std::vector<float> output_{0.0f};
  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  float normGain_ = 1.0f; // loudness normalization to kTargetLoudnessDb
  std::optional<float> modelInputLevelDbU_;
  float inputCalibrationGain_ = 1.0f;
  float slimmableSize_ = 1.0f;
};

} // namespace ardor
