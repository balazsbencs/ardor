// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace nam
{

/// \brief Default time a runtime parameter takes to travel to a newly committed value.
///
/// This is the one number that trades control latency against how smooth a moving control
/// sounds. The largest step left in the output is roughly
///
///     min(control velocity, range / ramp seconds) x block duration
///
/// so the ramp is a velocity limiter and nothing more: it binds only when the control is
/// moved faster than the ramp can follow. On an unhurried move the control's own speed is
/// the smaller term and the ramp length is irrelevant -- what is left then is set by the
/// block duration, which only matters for architectures that cannot update mid-block.
///
/// 200 ms was chosen by ear over 20/50/100 ms. Measured on a 49k-weight ConcatWaveNet with
/// a full-range flick in 50 ms, it leaves a peak step 5.5x smaller than 20 ms does.
constexpr float kDefaultParamRampSeconds = 0.200f;

/// \brief Real-time-safe linear ramp over a fixed-size vector of values.
///
/// Parametric models commit a whole parameter vector at once, so every value shares one
/// ramp length and they all arrive together. A linear ramp is used rather than a one-pole
/// because it terminates: once it lands there is no residual offset left to keep
/// multiplying down, no denormal tail, and the settled values are bit-identical to what
/// the host committed -- which is what lets the settled-state code path stay exactly what
/// it was before smoothing existed.
///
/// Values may be individually excluded from smoothing via the mask passed to Configure().
/// Excluded values jump straight to their target. This is how switch-type parameters keep
/// their integral indices: a blended one-hot is a conditioning vector no model was trained
/// on, and the encoders downstream expect an exact index.
///
/// This class carries no synchronization of its own. It is owned by the model and inherits
/// the serialization contract documented on IParametricControl.
class ParamRamp
{
public:
  /// \brief Size the ramp and set how long a re-target takes. Allocates; never call from
  /// the audio thread.
  ///
  /// \param num_values Number of values in the vector.
  /// \param sample_rate Rate the owning model is run at. A non-positive rate disables
  ///        ramping.
  /// \param seconds Ramp duration. Zero disables ramping.
  /// \param smoothed Per-value mask, non-zero meaning "smooth this value"; values whose
  ///        entry is zero always jump. An empty span smooths every value. Typed as char
  ///        rather than bool so callers can pass a contiguous container -- std::vector<bool>
  ///        is bit-packed and cannot form a span.
  void Configure(const size_t num_values, const double sample_rate, const float seconds,
                 const std::span<const char> smoothed = {})
  {
    _current.assign(num_values, 0.0f);
    _target.assign(num_values, 0.0f);
    _start.assign(num_values, 0.0f);
    _step.assign(num_values, 0.0f);
    _smoothed.assign(num_values, static_cast<char>(1));
    for (size_t i = 0; i < smoothed.size() && i < num_values; ++i)
      _smoothed[i] = smoothed[i];

    // Rounded, not truncated: `seconds` is a float, so an exact duration such as 10 ms is
    // really 0.00999999977, and truncating 48000 * that gives 479 samples rather than 480.
    _ramp_length = (sample_rate > 0.0 && seconds > 0.0f)
                     ? static_cast<int>(std::lround(sample_rate * static_cast<double>(seconds)))
                     : 0;
    _ramp_samples = 0;
    _position = 0;
  }

  /// \brief Jump to `values` immediately, cancelling any ramp in flight.
  void Snap(const std::span<const float> values)
  {
    const auto n = std::min(values.size(), _current.size());
    std::copy_n(values.begin(), n, _target.begin());
    std::copy_n(values.begin(), n, _current.begin());
    std::fill(_step.begin(), _step.end(), 0.0f);
    _ramp_samples = 0;
    _position = 0;
  }

  /// \brief Begin travelling to `values` from wherever the ramp currently sits.
  ///
  /// Re-targeting mid-ramp restarts from the current value, so the trajectory stays
  /// continuous no matter how often the host commits.
  ///
  /// \return true if the current values changed as a side effect (i.e. the ramp is
  ///         disabled or every value was excluded from smoothing, so this was a jump).
  ///         A caller that caches work derived from Current() can use this to know it
  ///         must recompute now rather than on the next Advance().
  bool SetTarget(const std::span<const float> values)
  {
    const auto n = std::min(values.size(), _target.size());
    if (std::equal(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(n), _target.begin()))
      return false;
    std::copy_n(values.begin(), n, _target.begin());

    if (_ramp_length <= 0)
    {
      std::copy(_target.begin(), _target.end(), _current.begin());
      std::fill(_step.begin(), _step.end(), 0.0f);
      _ramp_samples = 0;
      _position = 0;
      return true;
    }

    bool jumped = false;
    bool ramping = false;
    for (size_t i = 0; i < _current.size(); ++i)
    {
      if (_smoothed[i] == 0)
      {
        // Excluded from smoothing: jump now and contribute no slope.
        jumped |= _current[i] != _target[i];
        _current[i] = _target[i];
        _step[i] = 0.0f;
        continue;
      }
      _step[i] = (_target[i] - _current[i]) / static_cast<float>(_ramp_length);
      ramping |= _step[i] != 0.0f;
    }
    std::copy(_current.begin(), _current.end(), _start.begin());
    _ramp_samples = ramping ? _ramp_length : 0;
    _position = 0;
    return jumped;
  }

  /// \brief Advance the ramp by `num_samples`.
  /// \return true if the current values changed.
  bool Advance(const int num_samples)
  {
    if (!IsRamping() || num_samples <= 0)
      return false;

    _position = std::min(_position + num_samples, _ramp_samples);
    if (_position >= _ramp_samples)
    {
      // Land on the committed values exactly rather than on an accumulated approximation.
      std::copy(_target.begin(), _target.end(), _current.begin());
      _ramp_samples = 0;
      _position = 0;
      return true;
    }
    for (size_t i = 0; i < _current.size(); ++i)
      _current[i] = _start[i] + _step[i] * static_cast<float>(_position);
    return true;
  }

  bool IsRamping() const { return _position < _ramp_samples; }

  /// \brief Samples left before the ramp lands; zero when settled.
  int RemainingSamples() const { return _ramp_samples - _position; }

  /// \brief Values in effect for the next sample to be produced.
  const std::vector<float>& Current() const { return _current; }

  /// \brief Per-sample slope of the ramp in flight; zero for every value when settled.
  const std::vector<float>& Step() const { return _step; }

  /// \brief The values most recently committed.
  const std::vector<float>& Target() const { return _target; }

  size_t Size() const { return _current.size(); }

private:
  std::vector<float> _current;
  std::vector<float> _target;
  std::vector<float> _start;
  std::vector<float> _step;
  // char rather than bool: std::vector<bool> is a bit-packed proxy container, and this is
  // indexed per value on the audio path.
  std::vector<char> _smoothed;

  /// Configured ramp duration in samples; zero when ramping is disabled.
  int _ramp_length = 0;
  /// Duration of the ramp currently in flight; zero when settled.
  int _ramp_samples = 0;
  int _position = 0;
};

} // namespace nam
