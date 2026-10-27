// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <span>
#include <string>
#include <vector>

#include "json.hpp"

namespace nam
{

/// \brief Self-describing specification for one runtime control parameter.
///
/// Mirrors the Python-side ParamSpec. The list order in which these are exposed is
/// significant: a spec's position is the positional index into the parameter vector
/// passed to SetParams(). `name` and the [min, max] range are metadata for the
/// host/plugin UI (labeled knobs, clamping) and do NOT affect the forward pass,
/// normalization, or weight loading. `defaultValue` is the nominal value the network
/// is conditioned on for the export snapshot.
struct ParamSpec
{
  std::string name;
  float min;
  float max;
  float defaultValue;
  std::string type;
  std::vector<std::string> enum_names;

  int num_inputs() const
  {
    if (type == "switch")
      return static_cast<int>(enum_names.size());
    return 1;
  }
};

/// \brief Parse `config["params"]` into a vector of ParamSpec, applying the shared
/// validation rules mirrored from Python's ParamSpec.__post_init__ (nam/models/parametric/_spec.py).
///
/// `model_name` is used only to prefix error messages (e.g. "HyperWaveNet config: ...").
/// \throws std::runtime_error on any malformed or invalid entry.
std::vector<ParamSpec> parse_param_specs(const nlohmann::json& config, const std::string& model_name);

/// \brief Interface for DSP objects that support runtime parameter control.
///
/// Parametric DSPs implement this alongside DSP. Hosts discover the capability
/// via dynamic_cast<IParametricControl*>(dsp.get()).
///
/// Contract:
/// - Implementations of this interface may be intentionally unsynchronized for
///   realtime performance.
/// - The host must externally serialize SetParams(), GetParams(), process(),
///   Reset(), prewarm(), and destruction so they do not overlap.
/// - A common pattern is for the audio thread to call SetParams() between
///   process() blocks, after the previous block has completed and before the
///   next block begins.
/// - SetParams() is allocation-free after construction (storage is pre-sized
///   from the parameter count at object creation).
///
/// Smoothing is left to the implementation, because whether a control *can* move without a
/// step depends on how it reaches the audio -- so the two behaviours below are contractual,
/// but which one an implementation exhibits is documented on that class, not here:
/// - An implementation that smooths treats a committed vector as a destination rather than
///   something applied instantly, and GetParams() then reports that destination rather than
///   the value currently in effect.
/// - An implementation that does not smooth applies the committed vector on the next
///   process() call, and GetParams() reflects it immediately.
/// - Either way, Reset() settles immediately on the committed vector rather than ramping into
///   it: a reset is a stream restart, and a freshly loaded model must not glide from its
///   defaults to the host's restored values.
class IParametricControl
{
public:
  virtual ~IParametricControl() = default;

  /// Replace the current parameter vector consumed by the next process() block.
  /// This call is not implicitly thread-safe; see the interface contract above.
  /// \throws std::invalid_argument if params.size() != ParamDim().
  virtual void SetParams(std::span<const float> params) = 0;

  /// Read back the currently committed parameter vector.
  /// Reflects the last SetParams() call, or nominal params at construction.
  /// The returned span aliases internal storage and is invalidated by the next
  /// successful SetParams() call or object destruction.
  virtual std::span<const float> GetParams() const = 0;

  /// Dimensionality of the parameter vector accepted by SetParams().
  virtual int ParamDim() const = 0;

  /// Per-parameter specifications, in positional order.
  /// specs[i] describes the parameter at index i of the SetParams()/GetParams() vector.
  /// The host/plugin UI uses this to render labeled controls with correct ranges.
  /// The returned reference aliases internal storage that lives for the object's lifetime.
  virtual const std::vector<ParamSpec>& GetParamSpecs() const = 0;
};

} // namespace nam
