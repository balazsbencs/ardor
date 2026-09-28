#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
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

void verifySteadyReverbAutomation(const char* mode, const char* key, float maximumErrorStep)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", mode);
  require(descriptor != nullptr, std::string(mode) + " descriptor exists");
  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 1.0f;
  params[key] = 0.0f;
  ardor::DaisyFxProcessor automated;
  ardor::DaisyFxProcessor reference;
  std::string error;
  require(automated.configure("reverb", params, 48000.0f, error), error);
  require(reference.configure("reverb", params, 48000.0f, error), error);

  ardor::StereoSample previousAutomated{};
  ardor::StereoSample previousReference{};
  float observedStep = 0.0f;
  for (int frame = 0; frame < 3 * 48000; ++frame) {
    if (frame == 2 * 48000) require(automated.setParameterTarget(key, 1.0f), "automate reverb control");
    const float input = 0.25f * std::sin(6.28318530718f * 173.0f * frame / 48000.0f);
    const auto changed = automated.process({input, input});
    const auto unchanged = reference.process({input, input});
    if (frame >= 2 * 48000) {
      const float changeStep = (changed.left - previousAutomated.left)
                             - (unchanged.left - previousReference.left);
      observedStep = std::max(observedStep, std::fabs(changeStep));
    }
    previousAutomated = changed;
    previousReference = unchanged;
  }
  require(observedStep < maximumErrorStep,
          std::string(mode) + "/" + key + " should automate without a tap jump");
}

} // namespace

int main()
{
  verifySteadyReverbAutomation("room", "pre_delay", 0.04f);
  verifySteadyReverbAutomation("room", "param1", 0.04f);
  verifySteadyReverbAutomation("hall", "pre_delay", 0.03f);
  verifySteadyReverbAutomation("plate", "pre_delay", 0.03f);
  verifySteadyReverbAutomation("spring", "pre_delay", 0.04f);
  verifySteadyReverbAutomation("bloom", "pre_delay", 0.06f);
  verifySteadyReverbAutomation("chorale", "pre_delay", 0.02f);
  verifySteadyReverbAutomation("chorale", "param1", 0.03f);
  verifySteadyReverbAutomation("swell", "param2", 0.04f);
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
