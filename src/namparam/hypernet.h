// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "NAM/activations.h"
#include "parametric_control.h"

namespace nam
{
struct HypernetSpec;

class Hypernetwork
{
public:
  struct Target
  {
    std::string name;
    bool low_rank;
    int numel;
    int export_offset;
    int rank;
    int out_features;
    int rest_features;
    int output_width;
  };

  Hypernetwork(const HypernetSpec& spec, const std::vector<ParamSpec>& specs, std::span<const float> hypernet_state);

  int EncodedDim() const { return _encoded_dim; }
  static size_t SerializedStateCount(const HypernetSpec& spec, const std::vector<ParamSpec>& specs);

  /// Validate a raw parameter vector before ApplyConditioning. The host validates
  /// mailbox targets on its control thread; serialized audio-thread commits of
  /// those valid targets do not allocate. Throws
  /// std::invalid_argument on wrong size, non-finite values, or out-of-range/
  /// non-integer switch indices. Invalid callers may allocate while throwing.
  void ValidateParams(std::span<const float> raw_params) const;

  /// Build the conditioned weights into `out`. Real-time safe: performs no heap
  /// allocation (provided `out` is already sized to `base`) and never throws on the
  /// audio path. Parameter validity is the caller's responsibility via
  /// ValidateParams(); this path defensively clamps switch indices so an invalid
  /// vector can never produce an out-of-bounds write. `out` must NOT alias `base`.
  void ApplyConditioning(const std::vector<float>& base, std::span<const float> raw_params,
                         std::vector<float>& out) const;

private:
  struct TrunkLayer
  {
    Eigen::MatrixXf W;
    Eigen::VectorXf b;
    activations::Activation::Ptr activation;
  };

  static int _compute_hypernet_state_count(const HypernetSpec& spec, int encoded_dim);
  static void _validate_target(const Target& target);
  static void _validate_target_layouts(const std::vector<Target>& targets);

  void _encode_params(std::span<const float> raw_params) const;

  std::vector<ParamSpec> _specs;
  std::vector<Target> _targets;
  std::vector<TrunkLayer> _trunk_layers;
  Eigen::MatrixXf _final_weight;
  Eigen::VectorXf _final_bias;
  Eigen::VectorXf _anchor;
  int _encoded_dim = 0;
  mutable Eigen::VectorXf _encoded_scratch;
  mutable Eigen::VectorXf _hidden_a_scratch;
  mutable Eigen::VectorXf _hidden_b_scratch;
  mutable Eigen::VectorXf _flat_scratch;
  mutable Eigen::VectorXf _low_rank_factor_scratch;
  mutable Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> _delta_scratch;
};

struct HypernetSpec
{
  std::vector<int> hidden_sizes;
  activations::ActivationConfig activation;
  bool low_rank_mode;
  int rank;
  std::vector<Hypernetwork::Target> targets;
};

} // namespace nam
