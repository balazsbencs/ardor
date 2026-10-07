#include "daisyfx/pog3/Pog3Processor.h"

#include <cmath>

namespace ardor::pog3 {

struct Pog3Processor::State {
  Configuration configuration;
  Pog3SignalPath path;
  Values base{}, sound{};
  float generatedGain = 1, warp = 1;
  std::size_t remaining = 0, updates = 0;

  void apply(const Values& targets) noexcept {
    base = targets;
    sound = effectiveValues(configuration, base);
    const auto mode = static_cast<ExpressionMode>(choiceIndex(base[index(Parameter::ExpressionMode)], 7));
    const float endpoint = expressionEndpointValue(base);
    generatedGain = mode == ExpressionMode::Volume ? endpoint : 1;
    warp = mode == ExpressionMode::Warp ? endpoint : 1;
    path.setSoundValues(sound);
    path.setGeneratedGain(generatedGain);
    path.setWarp(warp);
    path.setFreeze(mode, expressionPosition(base), dryFreezeEligible(sound));
  }
};

Pog3Processor::Pog3Processor() noexcept = default;
Pog3Processor::~Pog3Processor() = default;

bool Pog3Processor::configure(const nlohmann::json& params, float sampleRate, std::string& error) {
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate != kSampleRate) {
    error = "POG3 requires a 48000 Hz sample rate"; return false;
  }
  Configuration configuration;
  if (!parseConfiguration(params, configuration, error)) return false;
  if (!kExperimentalFreezeEnabled && choiceIndex(configuration.base[index(Parameter::ExpressionMode)], 7) >= 5) {
    error = "Freeze expression modes are unavailable in this build.";
    return false;
  }
  auto next = std::make_unique<State>();
  next->configuration = configuration;
  next->apply(configuration.base);
  next->path.prepare();
  targets_.store(configuration.base);
  state_ = std::move(next);
  return true;
}

bool Pog3Processor::setParameterTarget(std::string_view key, float normalized) noexcept {
  const auto parameter = findParameter(key);
  return parameter && setParameterTarget(index(*parameter), normalized);
}
bool Pog3Processor::setParameterTarget(std::size_t parameterIndex, float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  if (!kExperimentalFreezeEnabled && parameterIndex == index(Parameter::ExpressionMode)
      && choiceIndex(normalized, 7) >= 5) return false;
  return targets_.setTarget(parameterIndex, normalized);
}
void Pog3Processor::reset() noexcept {
  if (!state_) return;
  state_->apply(targets_.read());
  state_->path.reset();
  state_->remaining = state_->updates = 0;
}
VoiceStageOutput Pog3Processor::process(PitchStereo input) noexcept {
  if (!state_) return {};
  if (state_->remaining == 0) {
    state_->apply(targets_.read());
    state_->remaining = kControlPeriod;
    ++state_->updates;
  }
  --state_->remaining;
  return state_->path.process(input);
}

Values Pog3Processor::baseValues() const noexcept { return state_ ? state_->base : defaultValues(); }
Values Pog3Processor::soundValues() const noexcept { return state_ ? state_->sound : defaultValues(); }
float Pog3Processor::generatedGainTarget() const noexcept { return state_ ? state_->generatedGain : 1; }
float Pog3Processor::warpTarget() const noexcept { return state_ ? state_->warp : 1; }
float Pog3Processor::filterCutoff() const noexcept { return state_ ? state_->path.stages().filterCutoff() : 20000; }
float Pog3Processor::filterEnvelope() const noexcept { return state_ ? state_->path.stages().filterEnvelope() : 0; }
std::size_t Pog3Processor::controlUpdates() const noexcept { return state_ ? state_->updates : 0; }
std::size_t Pog3Processor::transformCount() const noexcept { return state_ ? state_->path.transformCount() : 0; }
std::size_t Pog3Processor::deadlineMisses() const noexcept { return state_ ? state_->path.deadlineMisses() : 0; }
bool Pog3Processor::healthy() const noexcept { return state_ && state_->path.healthy(); }
SpectralFreeze::State Pog3Processor::freezeState() const noexcept { return state_ ? state_->path.freeze().state() : SpectralFreeze::State::Live; }
bool Pog3Processor::freezeLatched() const noexcept { return state_ && state_->path.freeze().latched(); }
std::size_t Pog3Processor::freezeCaptures() const noexcept { return state_ ? state_->path.freeze().captures() : 0; }
std::size_t Pog3Processor::freezeTargets() const noexcept { return state_ ? state_->path.freeze().targets() : 0; }
std::size_t Pog3Processor::freezeCapacityEvents() const noexcept { return state_ ? state_->path.freeze().capacityEvents() : 0; }

} // namespace ardor::pog3
