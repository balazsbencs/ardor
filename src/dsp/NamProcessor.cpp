#include "NamProcessor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

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

  model_->Reset(sampleRate, maxBlockSize);
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

bool NamProcessor::loaded() const
{
  return model_ != nullptr;
}

} // namespace ardor
