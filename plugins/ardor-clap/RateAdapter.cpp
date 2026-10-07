#include "RateAdapter.h"
#include <speex_resampler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardor::clap_audio {
namespace {
// Bound both integer factors for Speex's fractional-rate API. Continued
// fractions retain sub-sample precision over long sessions without rounding
// a fractional host rate to whole Hz or using an unbounded sinc phase table.
std::pair<unsigned, unsigned> rateRatio(double rate)
{
  const long double target = static_cast<long double>(rate) / 48000;
  // The upstream filter-length calculation multiplies its 160-tap base by
  // the rate numerator in 32 bits. Bound both factors to stay within that API.
  const auto maxDenominator = static_cast<uint64_t>(20000000 / std::max(1.L, target));
  long double remainder = target;
  uint64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
  for (;;) {
    const auto coefficient = std::floor(remainder);
    if (q1 && coefficient > (maxDenominator - q0) / q1) break;
    const auto a = static_cast<uint64_t>(coefficient);
    const auto p2 = p0 + a * p1, q2 = q0 + a * q1;
    p0 = p1; q0 = q1; p1 = p2; q1 = q2;
    const auto fraction = remainder - coefficient;
    if (fraction < 1e-18L) return {static_cast<unsigned>(p1), static_cast<unsigned>(q1)};
    remainder = 1 / fraction;
  }
  const auto k = (maxDenominator - q0) / q1;
  const auto p = p0 + k * p1, q = q0 + k * q1;
  if (std::fabs(static_cast<long double>(p) / q - target) < std::fabs(static_cast<long double>(p1) / q1 - target))
    return {static_cast<unsigned>(p), static_cast<unsigned>(q)};
  return {static_cast<unsigned>(p1), static_cast<unsigned>(q1)};
}
}

bool RateAdapter::supports(double rate) noexcept
{
  return std::isfinite(rate) && rate >= 1000 && rate <= 768000;
}
void RateAdapter::Destroy::operator()(SpeexResamplerState_* state) const noexcept
{ speex_resampler_destroy(state); }

void RateAdapter::prepare(double rate, unsigned engineLatency)
{
  if (!supports(rate)) throw std::runtime_error("Host rate must be between 1000 and 768000 Hz.");
  inputConverter_.reset(); outputConverter_.reset();
  unsigned inputLatency = 0, outputInputLatency = 0;
  if (rate != 48000) {
    int error = 0;
    const auto [numerator, denominator] = rateRatio(rate);
    inputConverter_.reset(speex_resampler_init_frac(1, numerator, denominator, static_cast<unsigned>(std::lround(rate)), 48000, 8, &error));
    if (!inputConverter_ || error) throw std::runtime_error("Cannot prepare input sample-rate converter.");
    outputConverter_.reset(speex_resampler_init_frac(2, denominator, numerator, 48000, static_cast<unsigned>(std::lround(rate)), 8, &error));
    if (!outputConverter_ || error) throw std::runtime_error("Cannot prepare output sample-rate converter.");
    inputLatency = speex_resampler_get_input_latency(inputConverter_.get());
    outputInputLatency = speex_resampler_get_input_latency(outputConverter_.get());
  }
  const double ratio = rate / 48000.;
  // Prime a bounded FIFO to cover the worst quantum assembly interval. SRC
  // phase stays continuous, so output consumption never depends on host blocks.
  schedulingDelay_ = static_cast<unsigned>(std::ceil(quantum * ratio));
  // CLAP latency is an integer number of host frames. Fractional FIR group delay
  // is rounded to the nearest frame (maximum half-frame quantization error).
  latency_ = schedulingDelay_ + inputLatency + static_cast<unsigned>(std::llround((outputInputLatency + engineLatency) * ratio));
  filterTail_ = 2 * (inputLatency + static_cast<unsigned>(std::ceil(outputInputLatency * ratio)));
  wetQueue_.resize(2 * schedulingDelay_ + 16);
  dryDelay_.resize(latency_);
  sceneDelay_.resize(inputLatency);
}

void RateAdapter::reset(int scene) noexcept
{
  if (inputConverter_) speex_resampler_reset_mem(inputConverter_.get());
  if (outputConverter_) speex_resampler_reset_mem(outputConverter_.get());
  input_.fill(0); left_.fill(0); right_.fill(0);
  std::fill(wetQueue_.begin(), wetQueue_.end(), Output{});
  std::fill(dryDelay_.begin(), dryDelay_.end(), 0);
  std::fill(sceneDelay_.begin(), sceneDelay_.end(), scene);
  position_ = 0; read_ = 0; write_ = available_ = schedulingDelay_;
  dryPosition_ = scenePosition_ = 0;
}

bool RateAdapter::tick(float input, float dry, int scene, Process process, void* context, Output& output) noexcept
{
  if (!sceneDelay_.empty()) {
    const auto delayed = sceneDelay_[scenePosition_];
    sceneDelay_[scenePosition_] = scene;
    scenePosition_ = (scenePosition_ + 1) % sceneDelay_.size();
    scene = delayed;
  }
  unsigned count = 1;
  convertedInput_[0] = input;
  if (inputConverter_) {
    unsigned consumed = 1; count = static_cast<unsigned>(convertedInput_.size());
    if (speex_resampler_process_float(inputConverter_.get(), 0, &input, &consumed, convertedInput_.data(), &count)
        || consumed != 1) return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    input_[position_++] = convertedInput_[i];
    if (position_ != quantum) continue;
    process(context, input_.data(), left_.data(), right_.data(), quantum, scene);
    position_ = 0;
    unsigned produced = quantum;
    const float* left = left_.data(); const float* right = right_.data();
    if (outputConverter_) {
      unsigned consumed = quantum; produced = static_cast<unsigned>(convertedLeft_.size());
      if (speex_resampler_process_float(outputConverter_.get(), 0, left, &consumed, convertedLeft_.data(), &produced)
          || consumed != quantum) return false;
      unsigned consumedRight = quantum, producedRight = static_cast<unsigned>(convertedRight_.size());
      if (speex_resampler_process_float(outputConverter_.get(), 1, right, &consumedRight, convertedRight_.data(), &producedRight)
          || consumedRight != quantum || producedRight != produced) return false;
      left = convertedLeft_.data(); right = convertedRight_.data();
    }
    if (available_ + produced > wetQueue_.size()) return false;
    for (unsigned j = 0; j < produced; ++j) {
      wetQueue_[write_] = {left[j], right[j], 0};
      write_ = (write_ + 1) % wetQueue_.size();
    }
    available_ += produced;
  }
  if (!available_) return false;
  output = wetQueue_[read_]; read_ = (read_ + 1) % wetQueue_.size(); --available_;
  output.dry = dryDelay_[dryPosition_]; dryDelay_[dryPosition_] = dry;
  dryPosition_ = (dryPosition_ + 1) % dryDelay_.size();
  return true;
}

} // namespace ardor::clap_audio
