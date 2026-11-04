// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#include "hyperwavenet.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "concat_wavenet.h"


namespace
{

std::vector<float> nominal_from_specs(const std::vector<nam::ParamSpec>& specs)
{
  std::vector<float> nominal;
  nominal.reserve(specs.size());
  for (const auto& spec : specs)
    nominal.push_back(spec.defaultValue);
  return nominal;
}

void reject_packed_layers(const nlohmann::json& config)
{
  if (!config.contains("layers") || !config.at("layers").is_array())
    return;
  for (const auto& layer : config.at("layers"))
  {
    if (layer.contains("packing") && !layer.at("packing").is_null())
      throw std::runtime_error("HyperWaveNet does not support packed layer arrays");
  }
}

nam::HypernetSpec parse_hypernet(const nlohmann::json& config)
{
  if (!config.contains("hypernet") || !config.at("hypernet").is_object())
    throw std::runtime_error("HyperWaveNet config missing 'hypernet' object.");

  const auto& hypernet_json = config.at("hypernet");
  nam::HypernetSpec hypernet;

  if (hypernet_json.contains("hidden_sizes"))
    hypernet.hidden_sizes = hypernet_json.at("hidden_sizes").get<std::vector<int>>();

  if (hypernet_json.contains("activation"))
    hypernet.activation = nam::activations::ActivationConfig::from_json(hypernet_json.at("activation"));
  else
    hypernet.activation = nam::activations::ActivationConfig::from_json("ReLU");

  if (hypernet.hidden_sizes.size() > 4 || std::any_of(hypernet.hidden_sizes.begin(), hypernet.hidden_sizes.end(),
      [](int n) { return n <= 0 || n > 512; }))
    throw std::runtime_error("HyperWaveNet supports up to four hidden layers of width 1 to 512");

  const auto mode = hypernet_json.value("mode", std::string("low_rank"));
  if (mode == "low_rank")
  {
    hypernet.low_rank_mode = true;
    if (!hypernet_json.contains("rank"))
      throw std::runtime_error("HyperWaveNet config: hypernet.rank is required when hypernet.mode is 'low_rank'.");
    hypernet.rank = hypernet_json.at("rank").get<int>();
  }
  else if (mode == "full")
  {
    hypernet.low_rank_mode = false;
    hypernet.rank = 0;
  }
  else
  {
    throw std::runtime_error("HyperWaveNet config: hypernet.mode must be 'full' or 'low_rank'.");
  }

  if (!hypernet_json.contains("delta_map"))
    throw std::runtime_error("HyperWaveNet config: hypernet.delta_map is required.");

  const auto& delta_map = hypernet_json.at("delta_map");
  if (!delta_map.is_array())
    throw std::runtime_error("HyperWaveNet config: hypernet.delta_map must be an array.");
  if (delta_map.size() > 2048)
    throw std::runtime_error("HyperWaveNet has too many weight targets");
  if (delta_map.empty())
    throw std::runtime_error("HyperWaveNet config: hypernet.delta_map must contain at least one target.");

  hypernet.targets.reserve(delta_map.size());
  for (size_t i = 0; i < delta_map.size(); ++i)
  {
    const auto& entry = delta_map.at(i);
    const auto where = "HyperWaveNet config: hypernet.delta_map[" + std::to_string(i) + "]";
    if (!entry.is_object())
      throw std::runtime_error(where + " must be an object.");
    if (!entry.contains("name") || !entry.contains("numel") || !entry.contains("export_offset"))
      throw std::runtime_error(where + " must define name/numel/export_offset.");

    const auto entry_mode = entry.value("mode", mode);
    nam::Hypernetwork::Target target{};
    target.name = entry.at("name").get<std::string>();
    target.low_rank = entry_mode == "low_rank";
    target.numel = entry.at("numel").get<int>();
    target.export_offset = entry.at("export_offset").get<int>();
    target.rank = 0;
    target.out_features = 0;
    target.rest_features = 0;
    target.output_width = 0;

    if (target.name.empty())
      throw std::runtime_error(where + " name must be non-empty.");
    if (target.numel <= 0 || target.numel > 16777216)
      throw std::runtime_error(where + " numel must be > 0.");
    if (target.export_offset < 0)
      throw std::runtime_error(where + " export_offset must be >= 0.");

    if (entry_mode == "full")
    {
      target.output_width = target.numel;
      hypernet.targets.push_back(std::move(target));
      continue;
    }

    if (entry_mode != "low_rank")
      throw std::runtime_error(where + " mode must be 'full' or 'low_rank'.");
    if (!hypernet.low_rank_mode)
      throw std::runtime_error(where + " is low_rank but hypernet.mode is 'full'; low-rank targets require "
                                       "hypernet.mode 'low_rank'.");

    target.rank = entry.value("rank", hypernet.rank);
    if (!entry.contains("out_features") || !entry.contains("rest_features"))
      throw std::runtime_error(where + " low-rank targets must define out_features/rest_features.");

    target.out_features = entry.at("out_features").get<int>();
    target.rest_features = entry.at("rest_features").get<int>();
    if (target.rank <= 0 || target.rank > 64)
      throw std::runtime_error(where + " low-rank targets require rank > 0.");
    if (target.out_features <= 0 || target.rest_features <= 0 || target.out_features > 4096 || target.rest_features > 4096)
      throw std::runtime_error(where + " low-rank target dimensions must be positive.");
    if (target.out_features * target.rest_features != target.numel)
      throw std::runtime_error(where + " numel must equal out_features * rest_features.");

    target.output_width = target.rank * (target.out_features + target.rest_features);
    hypernet.targets.push_back(std::move(target));
  }

  return hypernet;
}

void validate_target_ranges(const std::vector<nam::Hypernetwork::Target>& targets, const size_t base_len)
{
  for (const auto& target : targets)
  {
    const auto end = static_cast<size_t>(target.export_offset) + static_cast<size_t>(target.numel);
    if (end > base_len)
    {
      throw std::runtime_error("HyperWaveNet: target '" + target.name + "' export range exceeds base weight length.");
    }
  }
}

} // namespace

namespace nam
{
namespace wavenet
{

HyperWaveNet::HyperWaveNet(int in_channels, const std::vector<LayerArrayParams>& layer_array_params, float head_scale,
                           bool with_head, std::optional<HeadParams> head_params, std::unique_ptr<DSP> condition_dsp,
                           std::vector<ParamSpec> param_specs, const nam::HypernetSpec& hypernet_spec,
                           std::vector<float> base_weights, const std::span<const float> hypernet_state,
                           const double sample_rate, const bool fastA2)
: WaveNet(in_channels, layer_array_params, head_scale, with_head, std::move(head_params),
          std::vector<float>(base_weights), std::move(condition_dsp), sample_rate)
, _param_specs(std::move(param_specs))
, _params(nominal_from_specs(_param_specs))
, _base_weights(std::move(base_weights))
, _conditioned(_base_weights)
, _hypernet(hypernet_spec, _param_specs, hypernet_state)
, _dirty(true)
, _param_dim(static_cast<int>(_param_specs.size()))
{
  if (_param_specs.empty())
    throw std::invalid_argument("HyperWaveNet: param_specs must contain at least one parameter");
  if (fastA2) {
    _fast_a2 = std::make_unique<MutableA2>(_base_weights, sample_rate);
    _update_convolutions = std::any_of(hypernet_spec.targets.begin(), hypernet_spec.targets.end(),
      [](const auto& target) { return MutableA2::overlapsConvolution(target.export_offset, target.numel); });
  }
  _hypernet.ValidateParams(_params);
}

void HyperWaveNet::SetParams(const std::span<const float> params)
{

  _hypernet.ValidateParams(params);
  // Regenerating the whole weight set is this model's one expensive operation, so a commit
  // that does not move any control leaves the weights alone. A host that pushes the same
  // vector every block therefore does not re-run the hypernet forever.
  if (!std::equal(params.begin(), params.end(), _params.begin()))
  {
    std::copy(params.begin(), params.end(), _params.begin());
    _dirty = true;
  }
}

std::span<const float> HyperWaveNet::GetParams() const
{

  return std::span<const float>(_params);
}

int HyperWaveNet::ParamDim() const
{
  return _param_dim;
}

const std::vector<ParamSpec>& HyperWaveNet::GetParamSpecs() const
{
  return _param_specs;
}

void HyperWaveNet::process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames)
{

  if (_dirty)
  {
    _hypernet.ApplyConditioning(_base_weights, _params, _conditioned);
    if (_fast_a2) _fast_a2->setWeights(_conditioned, _update_convolutions);
    else WaveNet::set_weights_(_conditioned);
    _dirty = false;
  }
  if (_fast_a2) _fast_a2->process(input, output, num_frames);
  else WaveNet::process(input, output, num_frames);
}

int HyperWaveNet::GetPrewarmSamples() {
  return _fast_a2 ? _fast_a2->GetPrewarmSamples() : WaveNet::GetPrewarmSamples();
}

void HyperWaveNet::SetMaxBufferSize(int frames) {
  if (!_fast_a2) { WaveNet::SetMaxBufferSize(frames); return; }
  DSP::SetMaxBufferSize(frames);
  _fast_a2->SetPrewarmOnReset(false);
  _fast_a2->Reset(mHaveExternalSampleRate ? mExternalSampleRate : GetExpectedSampleRate(), frames);
}

std::unique_ptr<DSP> HyperWaveNetConfig::create(std::vector<float> weights, const double sampleRate)
{
  // Backstop for callers that build a HyperWaveNetConfig directly: the registered
  // get_dsp path already rejects condition_dsp in create_hyperwavenet_config() before
  // parse_config_json() can populate inner.condition_dsp, so this branch is unreachable
  // from a normal load. Kept so the invariant holds regardless of how the config was built.
  if (inner.condition_dsp != nullptr)
  {
    throw std::runtime_error(
      "HyperWaveNet does not yet support condition_dsp. Future support should extend the loader to own and "
      "condition nested WaveNet weights explicitly before applying delta_map export_offset ranges.");
  }

  const auto hypernet_state_count = nam::Hypernetwork::SerializedStateCount(hypernet, params);
  if (weights.size() <= hypernet_state_count)
  {
    throw std::runtime_error("HyperWaveNet: weight blob too short for base WaveNet plus hypernet state.");
  }

  const auto base_len = weights.size() - hypernet_state_count;
  if (base_len != wave_net_weight_count(inner))
    throw std::runtime_error("HyperWaveNet base weight count does not match its config");
  validate_target_ranges(hypernet.targets, base_len);

  std::vector<float> base_weights(weights.begin(), weights.begin() + static_cast<std::ptrdiff_t>(base_len));
  std::vector<float> hypernet_state(weights.begin() + static_cast<std::ptrdiff_t>(base_len), weights.end());

  const bool fastA2 = enableFastA2 && MutableA2::supports(inner);
  return std::make_unique<HyperWaveNet>(inner.in_channels, inner.layer_array_params, inner.head_scale, inner.with_head,
                                        std::move(inner.head_params), std::move(inner.condition_dsp), std::move(params),
                                        std::move(hypernet), std::move(base_weights), hypernet_state, sampleRate, fastA2);
}

std::unique_ptr<ModelConfig> create_hyperwavenet_config(const nlohmann::json& config, const double sampleRate)
{
  if (config.contains("condition_dsp") && !config.at("condition_dsp").is_null())
  {
    throw std::runtime_error(
      "HyperWaveNet does not yet support condition_dsp because delta_map export_offset values are currently "
      "defined only against the top-level WaveNet weight blob. The constructor shape is kept compatible so "
      "nested support can be added later in the loader without redesigning the DSP.");
  }

  reject_packed_layers(config);

  auto hyperwavenet_config = std::make_unique<HyperWaveNetConfig>();
  hyperwavenet_config->inner = parse_config_json(config, sampleRate);
  hyperwavenet_config->params = nam::parse_param_specs(config, "HyperWaveNet");
  hyperwavenet_config->hypernet = parse_hypernet(config);
  return hyperwavenet_config;
}

} // namespace wavenet
} // namespace nam
