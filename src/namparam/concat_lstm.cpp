// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#include "concat_lstm.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "concat_conditioner.h"

#include "NAM/registry.h"

namespace nam
{
namespace lstm
{

namespace
{

// Reject a config field the parser derives rather than reads, unless it happens to agree.
// The trainer omits these, so a disagreeing value means the file and the runtime disagree
// about the model's shape -- which would otherwise surface as a weight-count error whose
// message points at the wrong thing.
void require_derived_int(const nlohmann::json& config, const std::string& key, const int derived)
{
  if (config.contains(key) && !config.at(key).is_null() && config.at(key).get<int>() != derived)
    throw std::runtime_error("ConcatLSTM config: '" + key + "' must equal 1 + encoded param channels ("
                             + std::to_string(derived) + ").");
}

int positive_int(const nlohmann::json& config, const std::string& key, const int fallback)
{
  const int value = config.contains(key) && !config.at(key).is_null() ? config.at(key).get<int>() : fallback;
  if (value <= 0)
    throw std::runtime_error("ConcatLSTM config: '" + key + "' must be positive; got " + std::to_string(value) + ".");
  return value;
}

} // namespace

size_t concat_lstm_weight_count(const LSTMConfig& config)
{
  const auto hidden = static_cast<size_t>(config.hidden_size);
  const auto layers = static_cast<size_t>(config.num_layers);
  size_t count = 0;
  for (size_t layer = 0; layer < layers; ++layer)
  {
    const size_t input = layer == 0 ? static_cast<size_t>(config.input_size) : hidden;
    // xh -> ifgo matrix, bias, initial hidden state, initial cell state.
    count += 4 * hidden * (input + hidden) + 4 * hidden + hidden + hidden;
  }
  const auto out_channels = static_cast<size_t>(config.out_channels);
  count += out_channels * hidden + out_channels;
  return count;
}

std::unique_ptr<DSP> ConcatLSTMConfig::create(std::vector<float> weights, const double sampleRate)
{
  // nam::lstm::LSTM only assert()s that it consumed the blob exactly, so a short one walks
  // its iterator off the end in a release build. Check before handing it over.
  const auto expected = concat_lstm_weight_count(inner);
  if (weights.size() != expected)
    throw std::runtime_error("ConcatLSTM expected " + std::to_string(expected) + " weights, got "
                             + std::to_string(weights.size()) + ".");
  auto model = std::make_unique<LSTM>(
    inner.in_channels, inner.out_channels, inner.num_layers, inner.input_size, inner.hidden_size, weights, sampleRate);
  return std::make_unique<ConcatLSTM>(std::move(model), std::move(params), sampleRate);
}

std::unique_ptr<ModelConfig> create_concat_lstm_config(const nlohmann::json& config, const double sampleRate)
{
  (void)sampleRate;
  auto result = std::make_unique<ConcatLSTMConfig>();
  result->params = nam::parse_param_specs(config, "ConcatLSTM");

  if (!config.contains("hidden_size"))
    throw std::runtime_error("ConcatLSTM config must define hidden_size.");

  const auto inner_channels = 1 + encoded_param_dim(result->params);
  require_derived_int(config, "input_size", inner_channels);
  require_derived_int(config, "in_channels", inner_channels);

  result->inner.hidden_size = positive_int(config, "hidden_size", 0);
  result->inner.num_layers = positive_int(config, "num_layers", 1);
  result->inner.out_channels = positive_int(config, "out_channels", 1);
  result->inner.input_size = inner_channels;
  result->inner.in_channels = inner_channels;
  // Note: the trainer also writes "train_burn_in"/"train_truncate" into the config. They are
  // truncated-BPTT hyperparameters with no bearing on the forward pass, so the runtime
  // deliberately ignores them rather than parsing or validating them.
  return result;
}

} // namespace lstm
} // namespace nam
