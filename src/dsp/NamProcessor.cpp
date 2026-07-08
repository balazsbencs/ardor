#include "NamProcessor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace ardor {

NamProcessor::NamProcessor() = default;
NamProcessor::~NamProcessor() = default;

bool NamProcessor::load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize)
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
  model_->ResetAndPrewarm(sampleRate, maxBlockSize_);
  // ponytail: prewarm once on load; bypass/reset calls must stay cheap in the audio callback.
  model_->SetPrewarmOnReset(false);
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
  return output_[0];
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
    std::copy(output_.begin(), output_.begin() + static_cast<std::ptrdiff_t>(chunk), output + offset);
    offset += chunk;
  }
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

} // namespace ardor
