// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#include "hypernet.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace
{

using RowMajorMatrixXf = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

bool is_int_like(const float value)
{
  return std::isfinite(value) && std::trunc(value) == value;
}

void validate_param_spec(const nam::ParamSpec& spec)
{
  if (spec.name.empty())
    throw std::invalid_argument("Hypernetwork: parameter specs must have non-empty names");
  if (!std::isfinite(spec.min) || !std::isfinite(spec.max) || !std::isfinite(spec.defaultValue))
    throw std::invalid_argument("Hypernetwork: parameter " + spec.name + " must have finite min/max/default");

  if (spec.type == "continuous")
  {
    if (!spec.enum_names.empty())
      throw std::invalid_argument("Hypernetwork: continuous parameter " + spec.name + " cannot define enum_names");
    if (spec.min >= spec.max)
      throw std::invalid_argument("Hypernetwork: continuous parameter " + spec.name + " must satisfy min < max");
    if (spec.defaultValue < spec.min || spec.defaultValue > spec.max)
      throw std::invalid_argument("Hypernetwork: continuous parameter " + spec.name + " default must be within range");
    return;
  }

  if (spec.type != "switch")
    throw std::invalid_argument("Hypernetwork: unsupported parameter type " + spec.type);

  if (spec.enum_names.size() < 2)
    throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name + " must define at least two enum names");
  std::unordered_set<std::string> seen_enum_names;
  for (const auto& enum_name : spec.enum_names)
  {
    if (enum_name.empty())
      throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name + " enum names must be non-empty");
    if (!seen_enum_names.insert(enum_name).second)
      throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name + " enum names must be unique");
  }

  if (!is_int_like(spec.min) || !is_int_like(spec.max) || !is_int_like(spec.defaultValue))
    throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name
                                + " min/max/default must be integer indices");

  const auto expected_max = static_cast<int>(spec.enum_names.size()) - 1;
  const auto min_index = static_cast<int>(spec.min);
  const auto max_index = static_cast<int>(spec.max);
  const auto default_index = static_cast<int>(spec.defaultValue);
  if (min_index != 0 || max_index != expected_max)
  {
    std::ostringstream ss;
    ss << "Hypernetwork: switch parameter " << spec.name << " must use min/max index bounds [0, " << expected_max
       << "]";
    throw std::invalid_argument(ss.str());
  }
  if (default_index < min_index || default_index > max_index)
    throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name + " default index must be within range");
}

int compute_encoded_dim(const std::vector<nam::ParamSpec>& specs)
{
  // Note: duplicate parameter names are a config-policy concern (the host needs unique
  // names to map knobs) and do not affect the encode math, so that check lives in the
  // HyperWaveNet config parser (parse_params), not here.
  auto encoded_dim = 0;
  for (const auto& spec : specs)
  {
    validate_param_spec(spec);
    encoded_dim += spec.num_inputs();
  }
  return encoded_dim;
}

Eigen::MatrixXf read_row_major_matrix(std::span<const float> weights, size_t& offset, const int rows, const int cols,
                                      const std::string& name)
{
  if (rows <= 0 || cols <= 0)
    throw std::invalid_argument("Hypernetwork: " + name + " must have positive dimensions");

  const auto count = static_cast<size_t>(rows) * static_cast<size_t>(cols);
  if (offset + count > weights.size())
    throw std::invalid_argument("Hypernetwork: serialized state ended while reading " + name);

  Eigen::Map<const RowMajorMatrixXf> mapped(weights.data() + offset, rows, cols);
  offset += count;
  return mapped;
}

Eigen::VectorXf read_vector(std::span<const float> weights, size_t& offset, const int size, const std::string& name)
{
  if (size < 0)
    throw std::invalid_argument("Hypernetwork: " + name + " cannot have a negative size");

  const auto count = static_cast<size_t>(size);
  if (offset + count > weights.size())
    throw std::invalid_argument("Hypernetwork: serialized state ended while reading " + name);

  Eigen::Map<const Eigen::VectorXf> mapped(weights.data() + offset, size);
  offset += count;
  return mapped;
}

} // namespace

namespace nam
{

size_t Hypernetwork::SerializedStateCount(const HypernetSpec& spec, const std::vector<ParamSpec>& specs)
{
  return static_cast<size_t>(_compute_hypernet_state_count(spec, compute_encoded_dim(specs)));
}

int Hypernetwork::_compute_hypernet_state_count(const HypernetSpec& spec, const int encoded_dim)
{
  auto current_dim = encoded_dim;
  size_t trunk_param_count = 0;
  for (const auto hidden_size : spec.hidden_sizes)
  {
    if (hidden_size <= 0 || hidden_size > 512)
      throw std::invalid_argument("Hypernetwork: hidden_sizes entries must be positive");
    trunk_param_count += hidden_size * current_dim + hidden_size;
    current_dim = hidden_size;
  }

  size_t final_out = 0;
  for (const auto& target : spec.targets) {
    if (target.output_width <= 0 || target.output_width > 16777216)
      throw std::invalid_argument("Hypernetwork: invalid output width");
    final_out += target.output_width;
    if (final_out > 16777216) throw std::invalid_argument("Hypernetwork: output too large");
  }

  const auto final_param_count = final_out * current_dim + final_out;
  const size_t count = trunk_param_count + final_param_count + final_out;
  if (count > 16777216) throw std::invalid_argument("Hypernetwork: serialized state too large");
  return static_cast<int>(count);
}

void Hypernetwork::_validate_target(const Target& target)
{
  if (target.numel <= 0)
    throw std::invalid_argument("Hypernetwork: target " + target.name + " must have numel > 0");
  if (target.export_offset < 0)
    throw std::invalid_argument("Hypernetwork: target " + target.name + " must have export_offset >= 0");

  if (!target.low_rank)
  {
    if (target.output_width != target.numel)
      throw std::invalid_argument("Hypernetwork: full target " + target.name + " must have output_width == numel");
    return;
  }

  if (target.rank <= 0)
    throw std::invalid_argument("Hypernetwork: low-rank target " + target.name + " must have rank > 0");
  if (target.out_features <= 0 || target.rest_features <= 0)
    throw std::invalid_argument("Hypernetwork: low-rank target " + target.name + " must have positive dimensions");
  if (target.numel != target.out_features * target.rest_features)
    throw std::invalid_argument("Hypernetwork: low-rank target " + target.name + " has inconsistent numel");
  if (target.output_width != target.rank * (target.out_features + target.rest_features))
    throw std::invalid_argument("Hypernetwork: low-rank target " + target.name + " has inconsistent output_width");
}

void Hypernetwork::_validate_target_layouts(const std::vector<Target>& targets)
{
  struct TargetSpan
  {
    std::string name;
    int start;
    int end;
  };

  std::vector<TargetSpan> spans;
  spans.reserve(targets.size());
  for (const auto& target : targets)
  {
    const auto end = static_cast<long long>(target.export_offset) + static_cast<long long>(target.numel);
    if (end > std::numeric_limits<int>::max())
      throw std::invalid_argument("Hypernetwork: target " + target.name + " export range exceeds supported size");
    spans.push_back(TargetSpan{target.name, target.export_offset, static_cast<int>(end)});
  }
  std::sort(spans.begin(), spans.end(), [](const auto& lhs, const auto& rhs) { return lhs.start < rhs.start; });
  for (size_t i = 1; i < spans.size(); ++i)
  {
    if (spans[i].start < spans[i - 1].end)
    {
      throw std::invalid_argument("Hypernetwork: targets " + spans[i - 1].name + " and " + spans[i].name
                                  + " overlap in export offsets");
    }
  }
}

Hypernetwork::Hypernetwork(const HypernetSpec& spec, const std::vector<ParamSpec>& specs,
                           const std::span<const float> hypernet_state)
: _specs(specs)
, _targets(spec.targets)
, _encoded_dim(compute_encoded_dim(specs))
{
  if (_specs.empty())
    throw std::invalid_argument("Hypernetwork: expected at least one parameter spec");
  if (_targets.empty())
    throw std::invalid_argument("Hypernetwork: expected at least one target");
  if (_encoded_dim <= 0)
    throw std::invalid_argument("Hypernetwork: encoded parameter dimension must be positive");

  _validate_target_layouts(_targets);

  auto final_out = 0;
  auto any_low_rank = false;
  auto max_hidden = 0;
  for (const auto hidden_size : spec.hidden_sizes)
    max_hidden = std::max(max_hidden, hidden_size);

  auto max_low_rank_width = 0;
  auto max_out_features = 0;
  auto max_rest_features = 0;
  for (const auto& target : _targets)
  {
    _validate_target(target);
    final_out += target.output_width;
    if (target.low_rank)
    {
      any_low_rank = true;
      max_low_rank_width = std::max(max_low_rank_width, target.output_width);
      max_out_features = std::max(max_out_features, target.out_features);
      max_rest_features = std::max(max_rest_features, target.rest_features);
    }
  }

  if (!spec.low_rank_mode && any_low_rank)
    throw std::invalid_argument("Hypernetwork: low-rank targets require low_rank_mode");
  if (spec.low_rank_mode && any_low_rank && spec.rank <= 0)
    throw std::invalid_argument("Hypernetwork: low_rank_mode requires rank > 0");

  const auto expected_state_count = static_cast<int>(SerializedStateCount(spec, _specs));
  if (hypernet_state.size() != static_cast<size_t>(expected_state_count))
  {
    std::ostringstream ss;
    ss << "Hypernetwork: expected " << expected_state_count << " serialized values, found " << hypernet_state.size();
    throw std::invalid_argument(ss.str());
  }

  auto in_features = _encoded_dim;
  size_t offset = 0;
  for (size_t i = 0; i < spec.hidden_sizes.size(); ++i)
  {
    const auto hidden_size = spec.hidden_sizes[i];
    auto activation = activations::Activation::get_activation(spec.activation);
    if (activation == nullptr)
      throw std::invalid_argument("Hypernetwork: unsupported trunk activation");

    _trunk_layers.push_back(TrunkLayer{
      read_row_major_matrix(hypernet_state, offset, hidden_size, in_features, "trunk weight"),
      read_vector(hypernet_state, offset, hidden_size, "trunk bias"),
      std::move(activation),
    });
    in_features = hidden_size;
  }

  _final_weight = read_row_major_matrix(hypernet_state, offset, final_out, in_features, "final weight");
  _final_bias = read_vector(hypernet_state, offset, final_out, "final bias");
  _anchor = read_vector(hypernet_state, offset, final_out, "anchor");
  if (offset != hypernet_state.size())
    throw std::invalid_argument("Hypernetwork: serialized state length mismatch");

  _encoded_scratch.resize(_encoded_dim);
  _hidden_a_scratch.resize(max_hidden);
  _hidden_b_scratch.resize(max_hidden);
  _flat_scratch.resize(final_out);
  _low_rank_factor_scratch.resize(max_low_rank_width);
  _delta_scratch.resize(max_out_features, max_rest_features);
}

void Hypernetwork::ValidateParams(const std::span<const float> raw_params) const
{
  // Control-thread validation. Anything that would otherwise have to throw on the
  // audio path is checked here so ApplyConditioning() can stay exception-free.
  if (raw_params.size() != _specs.size())
  {
    std::ostringstream ss;
    ss << "Hypernetwork: expected " << _specs.size() << " raw params, found " << raw_params.size();
    throw std::invalid_argument(ss.str());
  }

  for (size_t i = 0; i < _specs.size(); ++i)
  {
    const auto& spec = _specs[i];
    const auto value = raw_params[i];
    if (!std::isfinite(value))
      throw std::invalid_argument("Hypernetwork: raw parameter " + spec.name + " must be finite");

    if (value < spec.min || value > spec.max)
      throw std::invalid_argument("Hypernetwork: parameter out of range");
    if (spec.type != "switch")
      continue;

    const auto rounded = std::round(value);
    if (rounded != value)
      throw std::invalid_argument("Hypernetwork: switch parameter " + spec.name + " index must be an integer");

    const auto index = static_cast<int>(rounded);
    if (index < 0 || index >= spec.num_inputs())
    {
      std::ostringstream ss;
      ss << "Hypernetwork: switch parameter " << spec.name << " index must be within [0, " << spec.num_inputs() - 1
         << "]";
      throw std::invalid_argument(ss.str());
    }
  }
}

void Hypernetwork::_encode_params(const std::span<const float> raw_params) const
{
  // Audio-thread path: must never throw and must never write out of bounds. Validity
  // is enforced up front by ValidateParams() on the control thread; here we only
  // clamp defensively so a stray value cannot corrupt the one-hot encoding.
  assert(raw_params.size() == _specs.size());

  _encoded_scratch.setZero();
  auto encoded_offset = 0;
  for (size_t i = 0; i < _specs.size(); ++i)
  {
    const auto& spec = _specs[i];
    const auto value = raw_params[i];

    if (spec.type == "switch")
    {
      const auto num_inputs = spec.num_inputs();
      const auto index = std::clamp(static_cast<int>(std::lround(value)), 0, num_inputs - 1);
      _encoded_scratch(encoded_offset + index) = 1.0f;
      encoded_offset += num_inputs;
      continue;
    }

    // Continuous (the only other type permitted past construction): signed min-max.
    const auto range = spec.max - spec.min;
    const auto frac = range != 0.0f ? (value - spec.min) / range : 0.0f;
    _encoded_scratch(encoded_offset) = -1.0f + 2.0f * frac;
    encoded_offset += 1;
  }
}

void Hypernetwork::ApplyConditioning(const std::vector<float>& base, const std::span<const float> raw_params,
                                     std::vector<float>& out) const
{
  _encode_params(raw_params);

  // Reuses out's storage when it is already sized to base (the steady-state case the
  // caller is expected to maintain), so this stays allocation-free on the audio path.
  out = base;

  const Eigen::VectorXf* current = &_encoded_scratch;
  auto current_dim = _encoded_dim;
  auto use_a = true;
  for (const auto& layer : _trunk_layers)
  {
    auto& next = use_a ? _hidden_a_scratch : _hidden_b_scratch;
    auto next_view = next.head(layer.b.size());
    next_view = layer.b;
    next_view.noalias() += layer.W * current->head(current_dim);
    layer.activation->apply(next.data(), layer.b.size());
    current = &next;
    current_dim = static_cast<int>(layer.b.size());
    use_a = !use_a;
  }

  _flat_scratch = _final_bias;
  _flat_scratch.noalias() += _final_weight * current->head(current_dim);

  auto flat_offset = 0;
  for (const auto& target : _targets)
  {
    // Both bounds are fixed at construction and validated against the base weights at
    // load time (see the consuming model's config parser), so they cannot vary per
    // call. Assert rather than throw to keep the audio path exception-free.
    assert(flat_offset + target.output_width <= _flat_scratch.size());
    assert(target.export_offset + target.numel <= static_cast<int>(out.size()));

    if (!target.low_rank)
    {
      Eigen::Map<Eigen::VectorXf>(out.data() + target.export_offset, target.numel) +=
        _flat_scratch.segment(flat_offset, target.numel);
      flat_offset += target.output_width;
      continue;
    }

    _low_rank_factor_scratch.head(target.output_width) = _flat_scratch.segment(flat_offset, target.output_width);
    _low_rank_factor_scratch.head(target.output_width) += _anchor.segment(flat_offset, target.output_width);

    const auto u_width = target.out_features * target.rank;
    Eigen::Map<const RowMajorMatrixXf> U(_low_rank_factor_scratch.data(), target.out_features, target.rank);
    Eigen::Map<const RowMajorMatrixXf> V(_low_rank_factor_scratch.data() + u_width, target.rank, target.rest_features);
    auto delta = _delta_scratch.topLeftCorner(target.out_features, target.rest_features);
    // lazyProduct (not operator*) so the small low-rank GEMM never allocates the
    // packing buffers Eigen's general matrix product can fall back to the heap for.
    delta.noalias() = U.lazyProduct(V);
    Eigen::Map<RowMajorMatrixXf>(out.data() + target.export_offset, target.out_features, target.rest_features) += delta;

    flat_offset += target.output_width;
  }
  assert(flat_offset == _flat_scratch.size());
}

} // namespace nam
