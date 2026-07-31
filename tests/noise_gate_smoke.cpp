#include "dynamics/NoiseGateProcessor.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

ardor::StereoSample render(ardor::NoiseGateProcessor& gate, float left, float right, int frames)
{
  ardor::StereoSample output{};
  for (int i = 0; i < frames; ++i) {
    const float polarity = i % 2 == 0 ? 1.0f : -1.0f;
    output = gate.process({left * polarity, right * polarity});
  }
  return output;
}

} // namespace

int main()
{
  const nlohmann::json settings{
    {"threshold_db", -50.0f},
    {"reduction_db", 80.0f},
    {"attack_ms", 2.0f},
    {"hold_ms", 50.0f},
    {"release_ms", 100.0f},
    {"hysteresis_db", 6.0f},
    {"sidechain_hpf_hz", 80.0f},
  };

  std::string error;
  ardor::NoiseGateProcessor gate;
  require(gate.configure(settings, 48000.0f, error), error);

  const auto closed = render(gate, 0.001f, 0.001f, 48000);
  require(std::isfinite(closed.left) && std::fabs(closed.left) < 2.0e-7f,
          "below-threshold signal reaches the configured reduction floor");

  const auto opened = render(gate, 0.1f, 0.1f, 4800);
  require(std::fabs(std::fabs(opened.left) - 0.1f) < 0.001f,
          "above-threshold signal opens the gate");

  const auto held = render(gate, 0.001f, 0.001f, 1200);
  require(std::fabs(held.left) > 0.0009f, "hold time keeps a decaying signal open");

  const auto released = render(gate, 0.001f, 0.001f, 48000);
  require(std::fabs(released.left) < 2.0e-7f, "release closes the gate smoothly");

  nlohmann::json hysteresisSettings = settings;
  hysteresisSettings["attack_ms"] = 0.1f;
  hysteresisSettings["hold_ms"] = 0.0f;
  ardor::NoiseGateProcessor hysteresisGate;
  require(hysteresisGate.configure(hysteresisSettings, 48000.0f, error), error);
  (void)render(hysteresisGate, 0.01f, 0.01f, 1000);
  const auto betweenThresholds = render(hysteresisGate, 0.0025f, 0.0025f, 1000);
  require(std::fabs(betweenThresholds.left) > 0.0024f,
          "hysteresis keeps the gate open between open and close thresholds");

  ardor::NoiseGateProcessor linkedGate;
  require(linkedGate.configure(settings, 48000.0f, error), error);
  const auto linked = render(linkedGate, 0.1f, 0.01f, 4800);
  require(std::fabs(std::fabs(linked.left) - std::fabs(linked.right) * 10.0f) < 0.0001f,
          "stereo-linked gate applies one gain to both channels");

  require(linkedGate.setParameterTarget("threshold_db", -20.0f),
          "threshold supports live updates");
  require(linkedGate.setParameterTarget("reduction_db", 40.0f),
          "reduction supports live updates");
  require(!linkedGate.setParameterTarget("unknown", 0.0f),
          "unknown live parameter is rejected");
  linkedGate.reset();
  const auto liveClosed = render(linkedGate, 0.001f, 0.001f, 48000);
  require(std::fabs(liveClosed.left) < 1.1e-5f,
          "live reduction target changes the closed floor");

  linkedGate.reset();
  const auto firstAfterReset = linkedGate.process({0.1f, 0.1f});
  linkedGate.reset();
  const auto repeatedAfterReset = linkedGate.process({0.1f, 0.1f});
  require(firstAfterReset.left == repeatedAfterReset.left, "reset is deterministic");

  ardor::NoiseGateProcessor invalid;
  require(!invalid.configure(settings, 0.0f, error), "invalid sample rate is rejected");
}
