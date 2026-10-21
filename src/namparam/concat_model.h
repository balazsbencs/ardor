// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "concat_conditioner.h"
#include "debug_param_api_guard.h"
#include "NAM/dsp.h"
#include "parametric_control.h"

namespace nam
{

/// \brief Base for every "concat" parametric model -- one that conditions an inner network
/// by concatenating the encoded controls onto the audio as extra input channels
/// (ConcatWaveNet, ConcatLSTM, ...).
///
/// The inner network sees 1 + encoded_param_dim input channels while this DSP exposes the
/// single audio channel a NAM host expects. Everything the two halves share lives here: the
/// composed ConcatConditioner (encoding, smoothing, and the concatenated input buffers), the
/// IParametricControl forwarding, and the reset/prewarm plumbing that keeps the inner model
/// in step. A concrete architecture only has to hand over its inner DSP; nothing in this
/// class depends on what that network is, since every call it makes is on the DSP interface.
///
/// These models smooth (see IParametricControl): the controls are extra input channels, so
/// the conditioning can move per sample and a knob move leaves no step. Continuous controls
/// are ramped; switches jump. See ConcatConditioner for the details.
class ConcatModel : public DSP, public IParametricControl
{
public:
  /// \param inner The conditioned network. Must accept 1 + encoded_param_dim input channels.
  /// \param param_specs Runtime control specs, in positional order.
  /// \param sample_rate Rate the model expects to run at; also sizes the smoothing ramp.
  /// \param model_name Architecture name, used only to prefix error messages.
  ConcatModel(std::unique_ptr<DSP> inner, std::vector<ParamSpec> param_specs, const double sample_rate,
              const std::string& model_name)
  : DSP(1, inner == nullptr ? 1 : inner->NumOutputChannels(), sample_rate)
  , _inner(std::move(inner))
  , _conditioner(std::move(param_specs))
  {
    if (_inner == nullptr)
      throw std::invalid_argument(model_name + ": inner model must not be null");
    if (_inner->NumInputChannels() != _conditioner.NumInputChannels())
      throw std::invalid_argument(model_name + ": inner model input channel count does not match encoded params");
    _conditioner.Configure(sample_rate);
  }

  void SetParams(const std::span<const float> params) override
  {
    DebugParamApiGuard guard(_debug_param_api_active);

    _conditioner.SetParams(params);
  }

  std::span<const float> GetParams() const override
  {
    DebugParamApiGuard guard(_debug_param_api_active);

    return _conditioner.Params();
  }

  int ParamDim() const override { return _conditioner.ParamDim(); }

  const std::vector<ParamSpec>& GetParamSpecs() const override { return _conditioner.ParamSpecs(); }

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
  {
    DebugParamApiGuard guard(_debug_param_api_active);

    assert(num_frames <= mMaxBufferSize);
    _inner->process(_conditioner.PrepareBlock(input[0], num_frames), output, num_frames);
  }

  void Reset(const double sampleRate, const int maxBufferSize) override
  {
    const auto prewarm_on_reset = GetPrewarmOnReset();
    _inner->SetPrewarmOnReset(false);
    try
    {
      _inner->Reset(sampleRate, maxBufferSize);
    }
    catch (...)
    {
      _inner->SetPrewarmOnReset(prewarm_on_reset);
      throw;
    }
    _inner->SetPrewarmOnReset(prewarm_on_reset);
    // Settle before DSP::Reset(), which prewarms through process(): a reset is a stream
    // restart, so the committed controls apply immediately rather than being ramped into.
    // This is also what keeps a model load from gliding -- the host resets the newly loaded
    // model, and two models need not share a parameter set at all.
    _conditioner.Configure(sampleRate);
    DSP::Reset(sampleRate, maxBufferSize);
  }

  void SetPrewarmOnReset(const bool prewarmOnReset) override
  {
    DSP::SetPrewarmOnReset(prewarmOnReset);
    _inner->SetPrewarmOnReset(prewarmOnReset);
  }

  int GetPrewarmSamples() override { return _inner->GetPrewarmSamples(); }

protected:
  void SetMaxBufferSize(const int maxBufferSize) override
  {
    DSP::SetMaxBufferSize(maxBufferSize);
    _conditioner.SetMaxBufferSize(maxBufferSize);
  }

private:
  std::unique_ptr<DSP> _inner;
  ConcatConditioner _conditioner;
  mutable std::atomic_flag _debug_param_api_active = ATOMIC_FLAG_INIT;
};

} // namespace nam
