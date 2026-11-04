// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "../hypernet.h"
#include "mutable_a2.h"
#include "NAM/model_config.h"
#include "../parametric_control.h"
#include "NAM/wavenet/model.h"

namespace nam
{
namespace wavenet
{

/// WaveNet whose weights are regenerated from the controls by a hypernetwork.
///
/// This model does NOT smooth (see IParametricControl). Its controls reach the audio only by
/// regenerating weights, and weights can only change between process() calls, so its transfer
/// function is quantised to the block grid no matter what. Ramping does not remove that
/// discontinuity, only relocate it -- from the host's control-commit rate up to the block
/// rate, where it is a clearly audible tone -- while regenerating the whole weight set every
/// block. It was implemented, measured and removed; do not add it back without measuring both
/// the spectrum and the cost (see the pinned upstream smoothing experiment).
class HyperWaveNet : public WaveNet, public IParametricControl
{
public:
  // `condition_dsp` remains in the constructor signature to mirror the WaveNet base
  // and keep the eventual nested-DSP extension localized to the config parser plus
  // weight-splitting path. The current loader rejects condition_dsp exports because
  // delta_map export_offset values are defined only against the top-level WaveNet
  // weight blob today.
  HyperWaveNet(int in_channels, const std::vector<LayerArrayParams>& layer_array_params, float head_scale,
               bool with_head, std::optional<HeadParams> head_params, std::unique_ptr<DSP> condition_dsp,
               std::vector<ParamSpec> param_specs, const nam::HypernetSpec& hypernet_spec,
               std::vector<float> base_weights, std::span<const float> hypernet_state, double sample_rate, bool fastA2 = false);

  void SetParams(std::span<const float> params) override;
  std::span<const float> GetParams() const override;
  int ParamDim() const override;
  const std::vector<ParamSpec>& GetParamSpecs() const override;

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, int num_frames) override;
  int GetPrewarmSamples() override;
  bool usesFastA2() const noexcept { return static_cast<bool>(_fast_a2); }
protected:
  void SetMaxBufferSize(int maxBufferSize) override;

private:
  std::unique_ptr<MutableA2> _fast_a2;
  bool _update_convolutions = true;
  std::vector<ParamSpec> _param_specs;
  std::vector<float> _params;
  std::vector<float> _base_weights;
  std::vector<float> _conditioned;
  nam::Hypernetwork _hypernet;
  bool _dirty = true;
  int _param_dim = 0;
};

struct HyperWaveNetConfig : public ModelConfig
{
  // This keeps the full parsed WaveNet config so adding nested condition_dsp support
  // later is a loader concern rather than a DSP redesign. For now
  // create_hyperwavenet_config() rejects condition_dsp explicitly.
  WaveNetConfig inner;
  std::vector<ParamSpec> params;
  nam::HypernetSpec hypernet;
  // The scalar implementation exists for parity tests; other production hosts
  // retain Eigen's generic backend until they have a specialized SIMD kernel.
#ifdef __aarch64__
  bool enableFastA2 = true;
#else
  bool enableFastA2 = false;
#endif

  HyperWaveNetConfig() = default;
  HyperWaveNetConfig(HyperWaveNetConfig&&) = default;
  HyperWaveNetConfig& operator=(HyperWaveNetConfig&&) = default;
  HyperWaveNetConfig(const HyperWaveNetConfig&) = delete;
  HyperWaveNetConfig& operator=(const HyperWaveNetConfig&) = delete;

  std::unique_ptr<DSP> create(std::vector<float> weights, double sampleRate) override;
};

std::unique_ptr<ModelConfig> create_hyperwavenet_config(const nlohmann::json& config, double sampleRate);

} // namespace wavenet
} // namespace nam
