// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "concat_model.h"
#include "NAM/lstm.h"
#include "NAM/model_config.h"
#include "parametric_control.h"

namespace nam
{
namespace lstm
{

/// LSTM conditioned by concatenating encoded controls with the audio input.
///
/// All of the parameter state and the plumbing that drives the inner network live in
/// ConcatModel; this class is only the LSTM-specific typing around it.
class ConcatLSTM : public ConcatModel
{
public:
  ConcatLSTM(std::unique_ptr<LSTM> lstm, std::vector<ParamSpec> param_specs, const double sample_rate)
  : ConcatModel(std::move(lstm), std::move(param_specs), sample_rate, "ConcatLSTM")
  {
  }
};

/// \brief Configuration for a ConcatLSTM model.
///
/// `inner.input_size` and `inner.in_channels` are both derived from the param specs
/// (1 + encoded_param_dim) rather than read from the config, mirroring the trainer, which
/// deliberately omits input_size from its exported config for the same reason.
struct ConcatLSTMConfig : public ModelConfig
{
  LSTMConfig inner;
  std::vector<ParamSpec> params;

  std::unique_ptr<DSP> create(std::vector<float> weights, double sampleRate) override;
};

/// \brief Number of weights a ConcatLSTM of this shape consumes.
///
/// Layout, in the order nam::lstm::LSTM reads it (which is the order the trainer's stock
/// LSTM exporter writes it, with the learned initial states standing in for the burnt-in
/// ones):
///
///   * per layer: the (input + hidden) -> ifgo matrix, row-major; the summed bias vector;
///     the initial hidden state; the initial cell state.
///   * the head weight matrix (out_channels x hidden_size), row-major, then its bias.
///
/// The first layer's input width is 1 + encoded_param_dim; every later layer's is
/// hidden_size.
size_t concat_lstm_weight_count(const LSTMConfig& config);

/// \brief Config parser for ConfigParserRegistry.
std::unique_ptr<ModelConfig> create_concat_lstm_config(const nlohmann::json& config, double sampleRate);

} // namespace lstm
} // namespace nam
