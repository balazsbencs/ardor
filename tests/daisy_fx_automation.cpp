#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ardor::StereoSample inputAt(int frame)
{
  const float left = 0.35f * std::sin(6.28318530718f * 173.0f * static_cast<float>(frame) / 48000.0f);
  const float right = 0.29f * std::sin(6.28318530718f * 277.0f * static_cast<float>(frame) / 48000.0f);
  return {left, right};
}

void renderAutomation(ardor::DaisyFxProcessor& processor, const std::string& label, int& frame)
{
  float previousLeft = 0.0f;
  float previousRight = 0.0f;
  for (int i = 0; i < 384; ++i, ++frame) {
    const auto output = processor.process(inputAt(frame));
    require(std::isfinite(output.left) && std::isfinite(output.right), label + " automation must remain finite");
    require(std::fabs(output.left) < 4.0f && std::fabs(output.right) < 4.0f,
            label + " automation must remain bounded");
    // The output includes deliberately nonlinear effects and delayed content,
    // so this is a deliberately generous discontinuity limit. It detects a
    // one-sample catastrophic topology/reset click without rejecting normal
    // saturation or transient-rich effect character.
    if (i != 0) {
      require(std::fabs(output.left - previousLeft) < 3.0f && std::fabs(output.right - previousRight) < 3.0f,
              label + " automation must not produce a catastrophic step");
    }
    previousLeft = output.left;
    previousRight = output.right;
  }
}

void verifyReverbAutomationContinuity(const ardor::DaisyFxDescriptor& descriptor,
                                      const std::string& key)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  params["mix"] = 1.0f;
  params[key] = 0.0f;
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", params, 48000.0f, error), descriptor.mode + ": " + error);

  ardor::StereoSample previous{};
  for (int frame = 0; frame < 12000; ++frame) {
    previous = processor.process(inputAt(frame));
  }
  require(processor.setParameterTarget(key, 1.0f), descriptor.mode + " must automate " + key);

  float maximumStep = 0.0f;
  for (int frame = 12000; frame < 18000; ++frame) {
    const auto output = processor.process(inputAt(frame));
    require(std::isfinite(output.left) && std::isfinite(output.right),
            descriptor.mode + "/" + key + " smoothed automation must remain finite");
    maximumStep = std::max(maximumStep, std::fabs(output.left - previous.left));
    maximumStep = std::max(maximumStep, std::fabs(output.right - previous.right));
    previous = output;
  }
  require(maximumStep < 0.75f,
          descriptor.mode + "/" + key + " automation must not create an audible-scale tap step");
}

} // namespace

int main()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    ardor::DaisyFxProcessor processor;
    std::string error;
    require(processor.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor), 48000.0f, error),
            descriptor.mode + ": " + error);
    int frame = 0;
    for (const auto& param : descriptor.params) {
      require(processor.setParameterTarget(param.key, 0.0f), descriptor.mode + " must accept " + param.key);
      renderAutomation(processor, descriptor.mode + "/" + param.key + " low", frame);
      require(processor.setParameterTarget(param.key, 1.0f), descriptor.mode + " must accept " + param.key);
      renderAutomation(processor, descriptor.mode + "/" + param.key + " high", frame);
      if (descriptor.blockType == "reverb" && param.key != "mix") {
        verifyReverbAutomationContinuity(descriptor, param.key);
      }
    }
  }
}
