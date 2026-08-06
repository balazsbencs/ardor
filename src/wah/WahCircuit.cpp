#include "wah/WahCircuit.h"

#include "dsp/DenormalGuard.h"

#include <algorithm>
#include <cmath>

namespace ardor {
namespace {

std::size_t clampIndex(float value, std::size_t limit)
{
  if (!(value >= 0.0f)) return 0; // Also catches NaN.
  const auto index = static_cast<std::size_t>(value);
  return index < limit ? index : limit;
}

} // namespace

bool WahCircuit::load(const std::filesystem::path& path, std::string& error)
{
  loaded_ = false;
  if (!readWahTable(path, table_, error)) return false;

  states_ = table_.header.states;
  ports_ = table_.header.ports;
  inputs_ = table_.header.inputs;
  if (ports_ != 2 || inputs_ != 2) {
    error = "wah table must have exactly 2 ports and 2 inputs";
    return false;
  }

  a_.assign(states_ * states_, 0.0f);
  b_.assign(states_ * inputs_, 0.0f);
  c_.assign(states_ * ports_, 0.0f);
  d_.assign(ports_ * states_, 0.0f);
  e_.assign(ports_ * inputs_, 0.0f);
  f_.assign(ports_ * ports_, 0.0f);
  g_.assign(states_, 0.0f);
  h_.assign(inputs_, 0.0f);
  k_.assign(ports_, 0.0f);
  state_.assign(states_, 0.0f);
  scratch_.assign(states_, 0.0f);

  loaded_ = true;
  position_ = -1.0f; // Force the first setPotPosition to populate the blend.
  setPotPosition(0.0f);
  reset();
  return true;
}

void WahCircuit::setPotPosition(float position)
{
  if (!loaded_) return;
  const float clamped = std::clamp(position, 0.0f, 1.0f);
  if (clamped == position_) return;
  position_ = clamped;

  const std::size_t last = table_.header.gridPot - 1;
  const float scaled = clamped * static_cast<float>(last);
  potLow_ = clampIndex(scaled, last);
  potHigh_ = std::min(potLow_ + 1, last);
  potBlend_ = scaled - static_cast<float>(potLow_);
  refreshBlend();
}

void WahCircuit::refreshBlend()
{
  const std::size_t stride = wahMatrixStride(table_.header);
  const float* low = table_.matrices.data() + potLow_ * stride;
  const float* high = table_.matrices.data() + potHigh_ * stride;
  const float t = potBlend_;

  std::size_t offset = 0;
  const auto blend = [&](std::vector<float>& destination) {
    for (std::size_t i = 0; i < destination.size(); ++i, ++offset) {
      destination[i] = low[offset] + t * (high[offset] - low[offset]);
    }
  };
  blend(a_);
  blend(b_);
  blend(c_);
  blend(d_);
  blend(e_);
  blend(f_);
  blend(g_);
  blend(h_);
  blend(k_);
}

float WahCircuit::interpolate(std::size_t port, float p1, float p2) const
{
  const auto& header = table_.header;
  const std::size_t grid = header.gridP;
  const std::size_t last = grid - 1;

  // Clamping rather than extrapolating matters: a transient that leaves the
  // measured range must saturate, not index outside the table.
  const float u = (p1 - header.p1Min) / (header.p1Max - header.p1Min);
  const float v = (p2 - header.p2Min) / (header.p2Max - header.p2Min);
  const float fu = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(last);
  const float fv = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(last);

  const std::size_t ia = clampIndex(fu, last - 1);
  const std::size_t ib = clampIndex(fv, last - 1);
  const float wa = fu - static_cast<float>(ia);
  const float wb = fv - static_cast<float>(ib);

  const std::size_t ports = ports_;
  const float* solutions = table_.solutions.data();
  const auto sample = [&](std::size_t pot, std::size_t b, std::size_t a) {
    return solutions[(((pot * grid + b) * grid) + a) * ports + port];
  };

  const auto plane = [&](std::size_t pot) {
    const float c00 = sample(pot, ib, ia);
    const float c10 = sample(pot, ib, ia + 1);
    const float c01 = sample(pot, ib + 1, ia);
    const float c11 = sample(pot, ib + 1, ia + 1);
    const float top = c00 + wa * (c10 - c00);
    const float bottom = c01 + wa * (c11 - c01);
    return top + wb * (bottom - top);
  };

  const float low = plane(potLow_);
  const float high = plane(potHigh_);
  return low + potBlend_ * (high - low);
}

float WahCircuit::process(float input)
{
  if (!loaded_) return input;
  ScopedDenormalGuard guard;

  const float u0 = input;
  const float u1 = supplyVolts_;

  // p = D x + E u
  float p1 = e_[0 * inputs_ + 0] * u0 + e_[0 * inputs_ + 1] * u1;
  float p2 = e_[1 * inputs_ + 0] * u0 + e_[1 * inputs_ + 1] * u1;
  for (std::size_t j = 0; j < states_; ++j) {
    p1 += d_[0 * states_ + j] * state_[j];
    p2 += d_[1 * states_ + j] * state_[j];
  }

  const float i1 = interpolate(0, p1, p2);
  const float i2 = interpolate(1, p1, p2);

  // y = G x + H u + K i
  float y = h_[0] * u0 + h_[1] * u1 + k_[0] * i1 + k_[1] * i2;
  for (std::size_t j = 0; j < states_; ++j) y += g_[j] * state_[j];

  // x' = A x + B u + C i
  for (std::size_t r = 0; r < states_; ++r) {
    float sum = b_[r * inputs_ + 0] * u0 + b_[r * inputs_ + 1] * u1;
    const float* row = a_.data() + r * states_;
    for (std::size_t c = 0; c < states_; ++c) sum += row[c] * state_[c];
    sum += c_[r * ports_ + 0] * i1 + c_[r * ports_ + 1] * i2;
    scratch_[r] = sum;
  }
  state_.swap(scratch_);
  return y;
}

void WahCircuit::reset()
{
  std::fill(state_.begin(), state_.end(), 0.0f);
  std::fill(scratch_.begin(), scratch_.end(), 0.0f);
  if (!loaded_) return;

  // Settle the bias network. The model carries the 9 V rail as an input, so
  // from a zeroed state the coupling caps charge over a real time constant —
  // hundreds of milliseconds. Running that silently here means the first note
  // after a preset change is not a thump.
  const auto settle = static_cast<std::size_t>(table_.header.sampleRate);
  for (std::size_t i = 0; i < settle; ++i) (void)process(0.0f);
}

} // namespace ardor
