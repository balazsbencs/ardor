// SPDX-License-Identifier: MIT
#include "film_wavenet.h"
#include "parametric_container.h"
#include "concat_lstm.h"
#include "wavenet/concat_wavenet.h"
#include "NAM/slimmable.h"
#include "preset/NamMetadata.h"
#include <algorithm>

namespace nam::ardor_parametric {
namespace {
// Hosts serialize control, size and audio calls as for IParametricControl.
class Container final : public DSP, public SlimmableModel, public IParametricControl {
public:
  explicit Container(const nlohmann::json& document) : DSP(1, 1, 48000) {
    const auto metadata = document.value("metadata", nlohmann::json::object());
    const double parentGain = metadata.contains("loudness") && metadata.at("loudness").is_number()
      ? std::pow(10.0, (-18.0 - metadata.at("loudness").get<double>()) / 20.0) : 1.0;
    for (const auto& entry : document.at("config").at("submodels")) {
      boundaries_.push_back(entry.at("max_value").get<double>());
      models_.push_back(create(entry.at("model")));
      const bool concat = entry.at("model").at("architecture") == "ConcatWaveNet";
      const auto& child = *models_.back();
      const double childGain = child.HasLoudness() ? std::pow(10.0, (-18.0 - child.GetLoudness()) / 20.0) : 1.0;
      const float gain = concat ? static_cast<float>(childGain / parentGain) : 1.0f;
      if (!std::isfinite(gain)) throw std::runtime_error("Invalid NAM tier loudness normalization");
      gains_.push_back(gain);
    }
    active_ = models_.size() - 1;
  }
  void process(NAM_SAMPLE** in, NAM_SAMPLE** out, int frames) override {
    models_[active_]->process(in, out, frames);
    const float gain = gains_[active_];
    if (gain != 1.0f) for (int i = 0; i < frames; ++i) out[0][i] *= gain;
  }
  void prewarm() override { models_[active_]->prewarm(); }
  int GetPrewarmSamples() override { return models_[active_]->GetPrewarmSamples(); }
  void SetPrewarmOnReset(bool value) override {
    DSP::SetPrewarmOnReset(value);
    for (auto& model : models_) model->SetPrewarmOnReset(value);
  }
  void Reset(double rate, int blockSize) override {
    mExternalSampleRate = rate;
    mHaveExternalSampleRate = true;
    DSP::SetMaxBufferSize(blockSize);
    // Prepare every tier off the audio thread on load. Later resets reuse capacity.
    for (auto& model : models_) {
      model->SetPrewarmOnReset(false);
      model->Reset(rate, blockSize);
      model->SetPrewarmOnReset(GetPrewarmOnReset());
    }
    if (GetPrewarmOnReset()) prewarm();
  }
  void SetSlimmableSize(double value) override {
    if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument("Invalid NAM tier ratio");
    size_t index = models_.size() - 1;
    for (size_t i = 0; i < boundaries_.size(); ++i) if (value < boundaries_[i]) { index = i; break; }
    if (index == active_) return;
    if (mHaveExternalSampleRate) {
      models_[index]->Reset(mExternalSampleRate, GetMaxBufferSize());
      // Size changes are prepared off the audio thread. Restore the selected
      // tier's conditioned silence history even after cheap bypass resets.
      if (!GetPrewarmOnReset()) models_[index]->prewarm();
    }
    active_ = index;
  }
  std::vector<double> GetSlimmableSizeBreakpoints() const override {
    return {boundaries_.begin(), boundaries_.end() - 1};
  }
  void SetParams(std::span<const float> values) override {
    for (auto& model : models_) dynamic_cast<IParametricControl&>(*model).SetParams(values);
  }
  std::span<const float> GetParams() const override { return controls().GetParams(); }
  int ParamDim() const override { return controls().ParamDim(); }
  const std::vector<ParamSpec>& GetParamSpecs() const override { return controls().GetParamSpecs(); }
private:
  const IParametricControl& controls() const { return dynamic_cast<const IParametricControl&>(*models_.front()); }
  std::vector<double> boundaries_;
  std::vector<std::unique_ptr<DSP>> models_;
  std::vector<float> gains_;
  size_t active_ = 0;
};
std::unique_ptr<DSP> createContainer(const nlohmann::json& document) {
  return std::make_unique<Container>(document);
}
}

std::unique_ptr<DSP> create(const nlohmann::json& document) {
  if (ardor::namParameterSpecs(document).empty()) throw std::runtime_error("Expected a parametric NAM model");
  const auto architecture = document.at("architecture").get<std::string>();
  std::unique_ptr<DSP> model;
  if (architecture == "SlimmableContainer") model = createContainer(document);
  else if (architecture == "ParametricWaveNet") model = ardor_film::create(document);
  else {
    const auto& config = document.at("config");
    const double rate = document.at("sample_rate").get<double>();
    auto parsed = architecture == "ConcatWaveNet"
      ? wavenet::create_concat_wavenet_config(config, rate)
      : lstm::create_concat_lstm_config(config, rate);
    auto weights = document.at("weights").get<std::vector<float>>();
    if (!std::all_of(weights.begin(), weights.end(), [](float v) { return std::isfinite(v); }))
      throw std::runtime_error("NAM weights must be finite");
    model = parsed->create(std::move(weights), rate);
  }
  if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
    throw std::runtime_error("Parametric NAM tiers require mono audio input and output");
  const auto metadata = document.value("metadata", nlohmann::json::object());
  if (metadata.contains("loudness") && metadata["loudness"].is_number()) {
    const double value = metadata["loudness"].get<double>();
    if (!std::isfinite(value)) throw std::runtime_error("Invalid NAM loudness");
    model->SetLoudness(value);
  }
  if (metadata.contains("input_level_dbu") && metadata["input_level_dbu"].is_number()) {
    const double value = metadata["input_level_dbu"].get<double>();
    if (!std::isfinite(value)) throw std::runtime_error("Invalid NAM input calibration");
    model->SetInputLevel(value);
  }
  return model;
}
} // namespace nam::ardor_parametric
