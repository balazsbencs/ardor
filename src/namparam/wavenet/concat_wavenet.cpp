// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#include "concat_wavenet.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

#include "NAM/registry.h"


namespace nam
{
namespace wavenet
{

namespace {
// The stock WaveNet loader consumes iterators before checking their length.
// Check the full layout first so a malformed uploaded model cannot read past
// its weight blob. Counts follow the pinned core's Layer/LayerArray/Head layout.
size_t conv_weights(int input, int output, int kernel, bool bias, int groups = 1)
{
  if (input <= 0 || output <= 0 || kernel <= 0 || groups <= 0
      || input > 4096 || output > 4096 || kernel > 4096
      || input % groups || output % groups)
    throw std::runtime_error("Invalid ConcatWaveNet convolution dimensions");
  return static_cast<size_t>(input / groups) * output * kernel + (bias ? output : 0);
}
size_t film_weights(const _FiLMParams& film, int condition, int input)
{
  return film.active ? conv_weights(condition, (film.shift ? 2 : 1) * input, 1, true, film.groups) : 0;
}
size_t weight_count(const WaveNetConfig& config)
{
  size_t count = 1; // final head_scale
  for (const auto& a : config.layer_array_params) {
    count += conv_weights(a.input_size, a.channels, 1, false);
    const int head_channels = a.head1x1_params.active ? a.head1x1_params.out_channels : a.bottleneck;
    count += conv_weights(head_channels, a.head_size, a.head_kernel_size, a.head_bias);
    for (size_t i = 0; i < a.dilations.size(); ++i) {
      const int z = a.gating_modes[i] == GatingMode::NONE ? a.bottleneck : 2 * a.bottleneck;
      count += conv_weights(a.channels, z, a.kernel_sizes[i], true, a.groups_input);
      count += conv_weights(a.condition_size, z, 1, false, a.groups_input_mixin);
      if (a.layer1x1_params.active)
        count += conv_weights(a.bottleneck, a.channels, 1, true, a.layer1x1_params.groups);
      if (a.head1x1_params.active)
        count += conv_weights(a.bottleneck, head_channels, 1, true, a.head1x1_params.groups);
      count += film_weights(a.conv_pre_film_params, a.condition_size, a.channels);
      count += film_weights(a.conv_post_film_params, a.condition_size, z);
      count += film_weights(a.input_mixin_pre_film_params, a.condition_size, a.condition_size);
      count += film_weights(a.input_mixin_post_film_params, a.condition_size, z);
      count += film_weights(a.activation_pre_film_params, a.condition_size, z);
      count += film_weights(a.activation_post_film_params, a.condition_size, a.bottleneck);
      count += film_weights(a._layer1x1_post_film_params, a.condition_size, a.channels);
      count += film_weights(a.head1x1_post_film_params, a.condition_size, head_channels);
    }
  }
  if (config.head_params) {
    const auto& head = *config.head_params;
    int input = head.in_channels;
    for (size_t i = 0; i < head.kernel_sizes.size(); ++i) {
      int output = i + 1 == head.kernel_sizes.size() ? head.out_channels : head.channels;
      count += conv_weights(input, output, head.kernel_sizes[i], true);
      input = output;
    }
  }
  return count;
}
} // namespace

std::unique_ptr<DSP> ConcatWaveNetConfig::create(std::vector<float> weights, const double sampleRate)
{
  if (weights.size() != weight_count(inner))
    throw std::runtime_error("ConcatWaveNet weight count does not match its config");
  if (inner.condition_dsp != nullptr)
    throw std::runtime_error("ConcatWaveNet does not support condition_dsp");
  auto wavenet = std::make_unique<WaveNet>(inner.in_channels, inner.layer_array_params, inner.head_scale,
                                           inner.with_head, std::move(inner.head_params), std::move(weights),
                                           std::move(inner.condition_dsp), sampleRate);
  return std::make_unique<ConcatWaveNet>(std::move(wavenet), std::move(params), sampleRate);
}

std::unique_ptr<ModelConfig> create_concat_wavenet_config(const nlohmann::json& config, const double sampleRate)
{
  if (config.contains("condition_dsp"))
    throw std::runtime_error("ConcatWaveNet does not support condition_dsp");
  if (!config.contains("layers") || !config.at("layers").is_array() || config.at("layers").empty())
    throw std::runtime_error("ConcatWaveNet config must define at least one layer array");

  auto result = std::make_unique<ConcatWaveNetConfig>();
  result->params = nam::parse_param_specs(config, "ConcatWaveNet");
  const auto inner_channels = 1 + encoded_param_dim(result->params);
  auto derived_config = config;
  derived_config["in_channels"] = inner_channels;
  for (size_t i = 0; i < derived_config.at("layers").size(); ++i)
  {
    auto& layer = derived_config.at("layers").at(i);
    if (layer.contains("packing") && !layer.at("packing").is_null())
      throw std::runtime_error("ConcatWaveNet does not support packed layer arrays");
    if (layer.contains("slimmable") && !layer.at("slimmable").is_null())
      throw std::runtime_error("ConcatWaveNet does not support slimmable inner WaveNets");
    if (i == 0)
    {
      if (layer.contains("input_size") && layer.at("input_size").get<int>() != inner_channels)
        throw std::runtime_error("ConcatWaveNet first layer input_size must equal 1 + encoded param channels");
      layer["input_size"] = inner_channels;
    }
    if (layer.contains("condition_size") && layer.at("condition_size").get<int>() != inner_channels)
      throw std::runtime_error("ConcatWaveNet layer condition_size must equal 1 + encoded param channels");
    layer["condition_size"] = inner_channels;
  }
  result->inner = parse_config_json(derived_config, sampleRate);
  return result;
}

} // namespace wavenet
} // namespace nam
