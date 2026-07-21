#include "NamProcessor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace {
// Matches the NAM plugin convention; normalizes model output to a consistent level.
constexpr double kTargetLoudnessDb = -18.0;
} // namespace

namespace ardor {

NamProcessor::NamProcessor() = default;
NamProcessor::~NamProcessor() = default;

bool NamProcessor::load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
                        float slimmableSize)
{
  try {
    model_ = nam::get_dsp(modelPath);
  } catch (const std::exception& e) {
    std::cerr << "Failed to load NAM model: " << e.what() << "\n";
    model_.reset();
    return false;
  }

  if (!model_) {
    return false;
  }

  const double expected = model_->GetExpectedSampleRate();
  if (expected > 0.0 && std::abs(expected - sampleRate) > 0.5) {
    std::cerr << "Model expects " << expected << " Hz, runtime is " << sampleRate << " Hz\n";
    model_.reset();
    return false;
  }

  sampleRate_ = sampleRate;
  maxBlockSize_ = std::max(1, maxBlockSize);
  input_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  output_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
  slimmableSize_ = std::clamp(std::isfinite(slimmableSize) ? slimmableSize : 0.0f, 0.0f, 1.0f);
  // SlimmableContainer defaults to its last/full submodel. Select the
  // requested tier before ResetAndPrewarm(), otherwise a Pi starts on the
  // expensive model even when a compact tier is available.
  if (auto* slim = dynamic_cast<nam::SlimmableModel*>(model_.get())) {
    slim->SetSlimmableSize(slimmableSize_);
  }
  model_->ResetAndPrewarm(sampleRate, maxBlockSize_);
  // ponytail: prewarm once on load; bypass/reset calls must stay cheap in the audio callback.
  model_->SetPrewarmOnReset(false);

  normGain_ = model_->HasLoudness()
    ? static_cast<float>(std::pow(10.0, (kTargetLoudnessDb - model_->GetLoudness()) / 20.0))
    : 1.0f;

  return true;
}

float NamProcessor::process(float input)
{
  if (!model_) {
    return input;
  }

  input_[0] = input;
  float* in[] = {input_.data()};
  float* out[] = {output_.data()};
  model_->process(in, out, 1);
  return output_[0] * normGain_;
}

void NamProcessor::processBlock(const float* input, float* output, size_t frames)
{
  if (!model_) {
    std::copy(input, input + frames, output);
    return;
  }

  size_t offset = 0;
  while (offset < frames) {
    const size_t chunk = std::min<size_t>(static_cast<size_t>(maxBlockSize_), frames - offset);
    std::copy(input + offset, input + offset + chunk, input_.begin());
    float* in[] = {input_.data()};
    float* out[] = {output_.data()};
    model_->process(in, out, static_cast<int>(chunk));
    const float ng = normGain_;
    std::transform(output_.begin(), output_.begin() + static_cast<std::ptrdiff_t>(chunk),
                   output + offset, [ng](float s) { return s * ng; });
    offset += chunk;
  }
}

void NamProcessor::clear()
{
  model_.reset();
  input_.assign(1, 0.0f);
  output_.assign(1, 0.0f);
  sampleRate_ = 0.0;
  maxBlockSize_ = 0;
  normGain_ = 1.0f;
  slimmableSize_ = 1.0f;
}

void NamProcessor::reset()
{
  if (!model_) {
    return;
  }

  model_->Reset(sampleRate_, maxBlockSize_);
}

bool NamProcessor::loaded() const
{
  return model_ != nullptr;
}

std::vector<double> NamProcessor::slimmableSizeBreakpoints() const
{
  if (!model_) return {};
  auto* slim = dynamic_cast<nam::SlimmableModel*>(model_.get());
  return slim ? slim->GetSlimmableSizeBreakpoints() : std::vector<double>{};
}

void NamProcessor::setSlimmableSize(double val)
{
  if (!model_) return;
  slimmableSize_ = static_cast<float>(std::clamp(val, 0.0, 1.0));
  auto* slim = dynamic_cast<nam::SlimmableModel*>(model_.get());
  if (slim) slim->SetSlimmableSize(slimmableSize_);
}

} // namespace ardor
