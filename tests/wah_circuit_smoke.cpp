#include "wah/WahCircuit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const char* tablePath()
{
  const char* fromEnv = std::getenv("ARDOR_WAH_TABLE");
  return fromEnv != nullptr ? fromEnv : "assets/wah/gcb95.wahtable";
}

} // namespace

int main()
{
  ardor::WahCircuit circuit;
  std::string error;
  require(circuit.load(tablePath(), error), error);
  require(circuit.loaded(), "the circuit should report itself loaded");

  // Stability and signal flow at every pot position. This is the assertion
  // that catches a bad discretization or a mis-blended matrix set.
  for (int p = 0; p <= 20; ++p) {
    circuit.reset();
    circuit.setPotPosition(p / 20.0f);
    float peak = 0.0f;
    for (int n = 0; n < 96000; ++n) {
      const float input = 0.5f * std::sin(static_cast<float>(n) * 0.05f);
      const float output = circuit.process(input);
      require(std::isfinite(output),
              "output must stay finite at pot position " + std::to_string(p / 20.0));
      peak = std::max(peak, std::fabs(output));
    }
    require(peak < 100.0f, "output must stay bounded at pot position " + std::to_string(p / 20.0));
    require(peak > 1e-4f, "the circuit should pass signal, not collapse to silence");
  }

  // Reset must fully clear state, or a preset change carries the previous
  // tail across.
  //
  // Position is set BEFORE the first reset deliberately: reset settles the
  // bias network using the current pot position's matrices, and the DC
  // operating point genuinely differs across the sweep. Comparing a reset at
  // one position against a reset at another would be comparing two different
  // circuits.
  circuit.setPotPosition(0.5f);
  circuit.reset();
  std::vector<float> first;
  first.reserve(1000);
  for (int n = 0; n < 1000; ++n) first.push_back(circuit.process(std::sin(n * 0.05f)));
  circuit.reset();
  for (int n = 0; n < 1000; ++n) {
    const float repeated = circuit.process(std::sin(n * 0.05f));
    require(std::fabs(repeated - first[static_cast<std::size_t>(n)]) < 1e-6f,
            "reset should restore identical behaviour");
  }

  // Table interpolation continuity. This is the failure mode specific to a
  // table-based solver, and it is audible as a click when a player rocks the
  // treadle slowly.
  {
    circuit.reset();
    circuit.setPotPosition(0.5f);
    for (int n = 0; n < 8192; ++n) circuit.process(0.2f * std::sin(n * 0.05f));
    const float before = circuit.process(0.1f);
    circuit.setPotPosition(0.5f + 1e-4f);
    const float after = circuit.process(0.1f);
    require(std::fabs(after - before) < 0.05f,
            "a tiny pot change must not step the output across a grid cell boundary");
  }

  // Sweeping the pot continuously must not produce jumps either. A snapped
  // matrix set shows up here even when a single nudge looks clean.
  {
    circuit.reset();
    circuit.setPotPosition(0.0f);
    for (int n = 0; n < 4096; ++n) circuit.process(0.2f * std::sin(n * 0.05f));
    float previous = circuit.process(0.2f);
    float previousDelta = 0.0f;
    float worst = 0.0f;
    for (int n = 1; n < 192000; ++n) {
      circuit.setPotPosition(static_cast<float>(n) / 192000.0f);
      const float output = circuit.process(0.2f * std::sin((4096 + n) * 0.05f));
      require(std::isfinite(output), "a swept pot must not produce non-finite output");
      const float delta = output - previous;
      worst = std::max(worst, std::fabs(delta - previousDelta));
      previousDelta = delta;
      previous = output;
    }
    require(worst < 0.5f, "a full pot sweep should not step the output");
  }

  // A missing table is an error, not a crash or a silent pass-through.
  {
    ardor::WahCircuit missing;
    std::string missingError;
    require(!missing.load("does/not/exist.wahtable", missingError),
            "loading a missing table should fail");
    require(!missingError.empty(), "a failed load should explain itself");
    require(!missing.loaded(), "a failed load should leave the circuit unloaded");
  }
  return 0;
}
