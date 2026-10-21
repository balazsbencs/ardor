// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "../concat_model.h"
#include "NAM/model_config.h"
#include "../parametric_control.h"
#include "NAM/wavenet/model.h"

namespace nam
{
namespace wavenet
{

/// WaveNet conditioned by concatenating encoded controls with the audio input.
///
/// All of the parameter state -- encoding, smoothing, and the concatenated input buffers --
/// and the plumbing that drives the inner network live in ConcatModel, so this class is only
/// the WaveNet-specific typing around it.
class ConcatWaveNet : public ConcatModel
{
public:
  ConcatWaveNet(std::unique_ptr<WaveNet> wavenet, std::vector<ParamSpec> param_specs, const double sample_rate)
  : ConcatModel(std::move(wavenet), std::move(param_specs), sample_rate, "ConcatWaveNet")
  {
  }
};

struct ConcatWaveNetConfig : public ModelConfig
{
  WaveNetConfig inner;
  std::vector<ParamSpec> params;

  ConcatWaveNetConfig() = default;
  ConcatWaveNetConfig(ConcatWaveNetConfig&&) = default;
  ConcatWaveNetConfig& operator=(ConcatWaveNetConfig&&) = default;
  ConcatWaveNetConfig(const ConcatWaveNetConfig&) = delete;
  ConcatWaveNetConfig& operator=(const ConcatWaveNetConfig&) = delete;

  std::unique_ptr<DSP> create(std::vector<float> weights, double sampleRate) override;
};

std::unique_ptr<ModelConfig> create_concat_wavenet_config(const nlohmann::json& config, double sampleRate);

} // namespace wavenet
} // namespace nam
