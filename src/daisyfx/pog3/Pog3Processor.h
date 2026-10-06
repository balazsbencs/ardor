#pragma once

#include "daisyfx/pog3/Pog3VoiceStages.h"

#include <memory>

namespace ardor::pog3 {

// Development core, not yet registered with the public effect factory.
// Configure and reset require exclusive lifecycle ownership. Configure compiles
// immutable endpoints and prepares all storage off the audio callback. Setters
// publish independent lock-free targets; process alone consumes DSP state.
// Freeze modes are rejected until their complete implementation is available.
class Pog3Processor {
public:
  static constexpr std::size_t kControlPeriod = 48;
  Pog3Processor() noexcept;
  ~Pog3Processor();
  Pog3Processor(const Pog3Processor&) = delete;
  Pog3Processor& operator=(const Pog3Processor&) = delete;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(std::string_view key, float normalized) noexcept;
  bool setParameterTarget(std::size_t parameterIndex, float normalized) noexcept;
  Values targetValues() const noexcept { return targets_.read(); }
  void reset() noexcept;
  VoiceStageOutput process(PitchStereo input) noexcept;

  // Audio-owner diagnostics; callers must not race these with process/reset.
  Values baseValues() const noexcept;
  Values soundValues() const noexcept;
  float generatedGainTarget() const noexcept;
  float warpTarget() const noexcept;
  float filterCutoff() const noexcept;
  float filterEnvelope() const noexcept;
  std::size_t controlUpdates() const noexcept;
  std::size_t transformCount() const noexcept;
  std::size_t deadlineMisses() const noexcept;
  bool healthy() const noexcept;

private:
  struct State;
  ParameterTargets targets_;
  std::unique_ptr<State> state_;
};

} // namespace ardor::pog3
