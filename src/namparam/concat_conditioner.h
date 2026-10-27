// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "NAM/compiler.h"
#include "NAM/dsp.h"
#include "param_ramp.h"
#include "parametric_control.h"

namespace nam
{

/// \brief Encoded conditioning-channel count for a set of parameter specs.
///
/// Continuous controls contribute one channel; switches contribute one per enum value.
/// Shared by the concat DSPs and their config parsers so the channel arithmetic lives in
/// exactly one place.
inline int encoded_param_dim(const std::vector<ParamSpec>& params)
{
  int dim = 0;
  for (const auto& spec : params)
    dim += spec.num_inputs();
  return dim;
}

/// \brief Owns the runtime-control state shared by every "concat" parametric model -- one
/// that conditions its inner network by concatenating the encoded controls onto the audio
/// as extra input channels (ConcatWaveNet, ConcatLSTM, ...).
///
/// It encapsulates everything those models share and nothing about the inner network
/// itself: the raw parameter vector, its encoding, the smoothing ramp, and the input
/// buffers handed to the inner model. A model composes one of these, forwards the
/// IParametricControl calls to it, and in process() does:
///
///     inner->process(conditioner.PrepareBlock(input[0], num_frames), output, num_frames);
///
/// Encoding: continuous controls are min-max mapped to [-1, 1]; switches are one-hot.
/// Smoothing runs in the encoded domain -- for continuous controls the encoding is affine,
/// so ramping the encoded value is the same as ramping the raw one, and the ramped values
/// are exactly what the conditioning channels want. Switches jump: a blended one-hot is a
/// conditioning vector no model was trained on.
///
/// This class carries no synchronization of its own; it inherits the serialization contract
/// documented on IParametricControl from the model that owns it.
class ConcatConditioner
{
public:
  explicit ConcatConditioner(std::vector<ParamSpec> param_specs)
  : _param_specs(std::move(param_specs))
  , _params(_param_specs.size())
  , _encoded_params(static_cast<size_t>(encoded_param_dim(_param_specs)))
  , _input_buffers(_encoded_params.size() + 1)
  , _input_ptrs(_encoded_params.size() + 1)
  {
    if (_param_specs.empty())
      throw std::invalid_argument("ConcatConditioner: param_specs must contain at least one parameter");
    for (size_t i = 0; i < _param_specs.size(); ++i)
      _params[i] = _param_specs[i].defaultValue;
    _encode_params();
  }

  /// Channels the inner network must accept: one audio channel plus the encoded controls.
  int NumInputChannels() const { return static_cast<int>(_input_buffers.size()); }

  /// Encoded conditioning-channel count (excludes the audio channel).
  size_t EncodedDim() const { return _encoded_params.size(); }

  int ParamDim() const { return static_cast<int>(_params.size()); }
  const std::vector<ParamSpec>& ParamSpecs() const { return _param_specs; }

  /// The most recently committed raw parameter vector. Aliases internal storage; invalidated
  /// by the next SetParams() call.
  std::span<const float> Params() const { return std::span<const float>(_params); }

  /// \brief Size the smoothing ramp for `sample_rate` and settle it on the current encoding.
  /// Allocates; call from construction and Reset() only, never from the audio thread.
  ///
  /// Settling (rather than ramping) is what a reset requires: a reset is a stream restart,
  /// so the committed controls take effect immediately, and a freshly loaded model does not
  /// glide from its defaults to the host's restored values.
  void Configure(const double sample_rate)
  {
    // Bypass resets use the prepared rate. Settle without creating a mask or
    // resizing the ramp on the audio thread.
    if (_configured && _configured_sample_rate == sample_rate)
    {
      _ramp.Snap(_encoded_params);
      return;
    }
    // Switch controls are one-hot encoded, so those channels are excluded from smoothing and
    // step to their target instead.
    std::vector<char> smoothed;
    smoothed.reserve(_encoded_params.size());
    for (const auto& spec : _param_specs)
      smoothed.insert(smoothed.end(), static_cast<size_t>(spec.num_inputs()),
                      static_cast<char>(spec.type != "switch"));
    _ramp.Configure(_encoded_params.size(), sample_rate, kDefaultParamRampSeconds, smoothed);
    _ramp.Snap(_encoded_params);
    _configured_sample_rate = sample_rate;
    _configured = true;
  }

  /// \brief Validate and store a raw parameter vector, re-encode it, and aim the ramp at it.
  /// Allocation-free after Configure(). \throws std::invalid_argument on size/switch errors.
  void SetParams(const std::span<const float> params)
  {
    if (params.size() != _params.size())
      throw std::invalid_argument("ConcatConditioner::SetParams: expected " + std::to_string(_params.size())
                                  + " params, got " + std::to_string(params.size()));
    for (size_t i = 0; i < _param_specs.size(); ++i)
    {
      const auto& spec = _param_specs[i];
      if (spec.type != "switch")
        continue;
      if (!_is_int_like(params[i]) || params[i] < 0.0f || params[i] >= static_cast<float>(spec.num_inputs()))
        throw std::invalid_argument("ConcatConditioner switch parameter '" + spec.name
                                    + "' must be an integer index within [0, " + std::to_string(spec.num_inputs() - 1)
                                    + "]");
    }
    std::copy(params.begin(), params.end(), _params.begin());
    _encode_params();
    _ramp.SetTarget(_encoded_params);
  }

  /// \brief Resize the input buffers. Allocates; call from the model's SetMaxBufferSize().
  void SetMaxBufferSize(const int maxBufferSize)
  {
    for (size_t ch = 0; ch < _input_buffers.size(); ++ch)
    {
      _input_buffers[ch].resize(maxBufferSize);
      _input_ptrs[ch] = _input_buffers[ch].data();
    }
  }

  /// \brief Fill the input buffers for one block and return the pointer array for the inner
  /// model. Copies `audio` into channel 0 and the (possibly ramping) encoded controls into
  /// the rest, then advances the ramp. Allocation-free.
  ///
  /// The conditioning channels are re-read per sample by the inner network regardless, so
  /// walking a control across the block instead of holding it constant costs one add per
  /// sample and leaves no step at the block boundary.
  NAM_SAMPLE** PrepareBlock(const NAM_SAMPLE* NAM_RESTRICT audio, const int num_frames)
  {
    assert(static_cast<size_t>(num_frames) <= _input_buffers[0].size());
    std::copy_n(audio, num_frames, _input_buffers[0].begin());

    if (!_ramp.IsRamping())
    {
      for (size_t ch = 0; ch < _encoded_params.size(); ++ch)
        std::fill_n(_input_buffers[ch + 1].begin(), num_frames, static_cast<NAM_SAMPLE>(_encoded_params[ch]));
    }
    else
    {
      const auto& current = _ramp.Current();
      const auto& step = _ramp.Step();
      const auto& target = _ramp.Target();
      const int ramped = std::min(num_frames, _ramp.RemainingSamples());
      for (size_t ch = 0; ch < _encoded_params.size(); ++ch)
      {
        NAM_SAMPLE* NAM_RESTRICT buffer = _input_buffers[ch + 1].data();
        float value = current[ch];
        const float slope = step[ch];
        for (int i = 0; i < ramped; ++i)
        {
          buffer[i] = static_cast<NAM_SAMPLE>(value);
          value += slope;
        }
        // Past the end of the ramp, use the committed value itself rather than the
        // accumulated one so the settled state is exact.
        const auto settled = static_cast<NAM_SAMPLE>(target[ch]);
        for (int i = ramped; i < num_frames; ++i)
          buffer[i] = settled;
      }
    }
    _ramp.Advance(num_frames);
    return _input_ptrs.data();
  }

private:
  static bool _is_int_like(const float value) { return std::isfinite(value) && std::trunc(value) == value; }

  void _encode_params()
  {
    size_t encoded_index = 0;
    for (size_t i = 0; i < _param_specs.size(); ++i)
    {
      const auto& spec = _param_specs[i];
      if (spec.type == "switch")
      {
        std::fill_n(_encoded_params.begin() + static_cast<std::ptrdiff_t>(encoded_index), spec.num_inputs(), 0.0f);
        _encoded_params[encoded_index + static_cast<size_t>(_params[i])] = 1.0f;
        encoded_index += static_cast<size_t>(spec.num_inputs());
      }
      else
      {
        const auto fraction = (_params[i] - spec.min) / (spec.max - spec.min);
        _encoded_params[encoded_index++] = -1.0f + 2.0f * fraction;
      }
    }
  }

  std::vector<ParamSpec> _param_specs;
  std::vector<float> _params;
  std::vector<float> _encoded_params;
  ParamRamp _ramp;
  double _configured_sample_rate = 0.0;
  bool _configured = false;
  std::vector<std::vector<NAM_SAMPLE>> _input_buffers;
  std::vector<NAM_SAMPLE*> _input_ptrs;
};

} // namespace nam
