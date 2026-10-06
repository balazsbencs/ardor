#include "daisyfx/pog3/Pog3VoiceStages.h"

#include <cmath>
#include <stdexcept>

namespace ardor::pog3 {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kMinimumQ = .70710678f;
constexpr float kControlSmooth = .0327839f; // 10 ms one-pole, advanced every 16 samples.
static_assert(pedal::SAMPLE_RATE == kSampleRate);
float unit(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }
float move(float current, float target, float amount) noexcept {
  return current + std::clamp(target - current, -amount, amount);
}
}

void Slew::set(float target, std::size_t samples) noexcept {
  if (target == target_) return;
  target_ = target;
  remaining_ = std::max<std::size_t>(samples, 1);
  step_ = (target_ - current_) / remaining_;
}
float Slew::tick() noexcept {
  if (remaining_ && --remaining_ == 0) current_ = target_;
  else if (remaining_) current_ += step_;
  return current_;
}

bool PlayingDetector::setSensitivity(float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  normalized = unit(normalized);
  gate_ = std::pow(10.0, (-42 - 24 * normalized) / 10.0);
  contrast_ = .8f - .72f * normalized;
  return true;
}
void PlayingDetector::reset() noexcept {
  fast_ = slow_ = reference_ = framePeak_ = blockPower_ = referencePower_ = 0;
  refractory_ = 720; frame_ = powerFrame_ = triggers_ = 0; armed_ = quiet_ = true;
}
bool PlayingDetector::process(PitchStereo input) noexcept {
  const double power = std::isfinite(input.left) && std::isfinite(input.right)
    ? .5 * (static_cast<double>(input.left) * input.left + static_cast<double>(input.right) * input.right) : 0;
  fast_ += .0103626011 * (power - fast_); // 2 ms power envelope.
  slow_ += .00069420337 * (power - slow_); // 30 ms power envelope.
  const double flux = (fast_ - slow_) / std::max(slow_, gate_);
  if (slow_ < gate_ * .25 && fast_ < gate_) { quiet_ = true; reference_ = referencePower_ = 0; }
  if (flux < contrast_ * .25) armed_ = true;
  refractory_ = std::min<std::size_t>(720, refractory_ + 1);
  const bool onset = armed_ && refractory_ == 720 && fast_ > gate_ && flux > contrast_
    && (quiet_ || fast_ > reference_ * (1 + contrast_));
  if (onset) { ++triggers_; refractory_ = 0; armed_ = quiet_ = false; }
  framePeak_ = std::max(framePeak_, fast_);
  if (++frame_ == 240) {
    reference_ = std::max(framePeak_, reference_);
    frame_ = 0; framePeak_ = 0;
  }
  // Release the peak only when 100 ms mean energy falls materially. A timed
  // peak release mistakes chord beating for new playing at high sensitivity.
  blockPower_ += power;
  if (++powerFrame_ == 4800) {
    const double mean = blockPower_ / 4800;
    if (referencePower_ > 0 && mean < referencePower_ * .7) {
      reference_ *= mean / referencePower_;
      referencePower_ = mean;
    } else referencePower_ = std::max(referencePower_, mean);
    powerFrame_ = 0; blockPower_ = 0;
  }
  return onset;
}

bool FilterAd::setTimes(float attackSeconds, float decaySeconds) noexcept {
  if (!std::isfinite(attackSeconds) || !std::isfinite(decaySeconds)) return false;
  attack_ = static_cast<std::size_t>(std::lround(std::clamp(attackSeconds, .005f, 3.0f) * kSampleRate));
  decay_ = static_cast<std::size_t>(std::lround(std::clamp(decaySeconds, .020f, 3.0f) * kSampleRate));
  return true;
}
void FilterAd::reset() noexcept { state_ = State::Idle; value_ = start_ = 0; elapsed_ = duration_ = 0; }
void FilterAd::trigger() noexcept {
  start_ = value_; elapsed_ = 0; duration_ = attack_; state_ = State::Attack;
}
float FilterAd::process() noexcept {
  if (state_ == State::Idle) return value_;
  const float x = static_cast<float>(++elapsed_) / duration_;
  value_ = state_ == State::Attack ? start_ + (1 - start_) * x : 1 - x;
  if (elapsed_ == duration_) {
    elapsed_ = 0;
    if (state_ == State::Attack) { value_ = 1; state_ = State::Decay; duration_ = decay_; }
    else { value_ = 0; state_ = State::Idle; }
  }
  return value_;
}

void FractionalHistory::prepare(std::size_t maximumDelay) {
  if (!maximumDelay || maximumDelay > 7200) throw std::invalid_argument("invalid POG3 delay length");
  maximum_ = maximumDelay; samples_.resize(maximumDelay + 4); reset();
}
void FractionalHistory::reset() noexcept { std::fill(samples_.begin(), samples_.end(), 0); write_ = 0; }
void FractionalHistory::write(float sample) noexcept {
  if (!samples_.empty()) samples_[write_] = std::isfinite(sample) ? sample : 0;
}
float FractionalHistory::read(float delay) const noexcept {
  if (samples_.empty() || !std::isfinite(delay)) return 0;
  delay = std::clamp(delay, 0.0f, static_cast<float>(maximum_));
  const auto integer = static_cast<std::size_t>(delay);
  const auto a = (write_ + samples_.size() - integer) % samples_.size();
  const auto b = a ? a - 1 : samples_.size() - 1;
  const float fraction = delay - integer;
  if (fraction == 0) return samples_[a];
  return (1 - fraction) * samples_[a] + fraction * samples_[b];
}
void FractionalHistory::advance() noexcept { if (!samples_.empty()) write_ = (write_ + 1) % samples_.size(); }

void StereoSpread::prepare() { history_[0].prepare(2400); history_[1].prepare(7200); prepared_ = true; reset(); }
bool StereoSpread::setAmount(float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  target_ = 7200 * unit(normalized);
  if (prepared_) taps_.SetTarget(target_);
  return true;
}
void StereoSpread::reset() noexcept {
  for (auto& history : history_) history.reset();
  taps_.Reset(); taps_.SetTarget(target_);
}
PitchStereo StereoSpread::process(PitchStereo input) noexcept {
  if (!prepared_) return input;
  const std::array<float, 2> x{input.left, input.right};
  PitchStereo result{};
  for (std::size_t side = 0; side < 2; ++side) {
    auto& history = history_[side]; history.write(x[side]);
    const float scale = side ? 1.0f : 1.0f / 3;
    const float a = history.read(taps_.from() * scale);
    const float value = taps_.active() ? (1 - taps_.mix()) * a + taps_.mix() * history.read(taps_.to() * scale) : a;
    (side ? result.right : result.left) = value;
    history.advance();
  }
  taps_.Advance();
  // The shared helper's 0.01-sample deadband must never leave an almost-zero
  // delay at the exact-off endpoint. Finish any audible fade before clearing it.
  if (target_ == 0 && !taps_.active() && taps_.to() != 0) { taps_.Reset(); taps_.SetTarget(0); }
  return result;
}

void StereoDoubling::prepare(std::size_t voice) {
  voice_ = voice;
  for (auto& history : history_) history.prepare(508);
  prepared_ = true; reset();
}
bool StereoDoubling::setDepth(float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  target_ = unit(normalized); depth_.set(target_); return true;
}
void StereoDoubling::reset() noexcept {
  for (auto& history : history_) history.reset();
  phase_ = {.37 * voice_, 1.5707963267948966 + .51 * voice_};
  read_ = targetRead_ = {384, 384}; tick_ = 0; largestStep_ = 0; depth_.reset(target_);
}
PitchStereo StereoDoubling::process(PitchStereo input) noexcept {
  if (!prepared_) return input;
  const float depth = depth_.tick();
  if (tick_++ % 16 == 0) for (std::size_t side = 0; side < 2; ++side) {
    targetRead_[side] = 384 + 72 * depth * static_cast<float>(std::sin(phase_[side]));
    phase_[side] = std::fmod(phase_[side] + 2 * kPi * (side ? .33 : .29) * 16 / kSampleRate, 2 * kPi);
  }
  const std::array<float, 2> source{input.left, input.right};
  PitchStereo result{};
  for (std::size_t side = 0; side < 2; ++side) {
    auto& history = history_[side]; history.write(source[side]);
    const float remaining = static_cast<float>(16 - (tick_ - 1) % 16);
    const float next = move(read_[side], targetRead_[side], std::min(.005f, std::fabs(targetRead_[side] - read_[side]) / remaining));
    largestStep_ = std::max(largestStep_, std::fabs(next - read_[side])); read_[side] = next;
    const float doubled = history.read(read_[side]);
    (side ? result.right : result.left) = depth == 0 ? source[side] : (1 - .5f * depth) * source[side] + .5f * depth * doubled;
    history.advance();
  }
  return result;
}

Pog3VoiceStages::Pog3VoiceStages() noexcept : values_(defaultValues()) { generatedGain_.reset(1); updateTargets(); }
bool Pog3VoiceStages::setGeneratedGain(float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  generatedGainTarget_ = unit(normalized); generatedGain_.set(generatedGainTarget_); return true;
}
bool Pog3VoiceStages::setValues(const Values& values) noexcept {
  for (const auto value : values) if (!std::isfinite(value)) return false;
  Values validated = values;
  for (auto& value : validated) value = unit(value);
  if (validated == values_) return true;
  values_ = validated; updateTargets(); return true;
}
void Pog3VoiceStages::updateTargets() noexcept {
  const auto raw = [&](Parameter p) { return values_[index(p)]; };
  inputGain_.set(physicalValue(Parameter::InputGain, raw(Parameter::InputGain)));
  master_.set(physicalValue(Parameter::MasterLevel, raw(Parameter::MasterLevel)));
  for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
    level_[voice].set(values_[index(Parameter::DryLevel) + voice]);
    const float p = values_[index(Parameter::DryPan) + voice];
    panLeft_[voice].set(p == .5f ? 1 : p == 1 ? 0 : 1.41421356237f * std::cos(p * static_cast<float>(kPi / 2)));
    panRight_[voice].set(p == .5f ? 1 : p == 0 ? 0 : 1.41421356237f * std::sin(p * static_cast<float>(kPi / 2)));
  }
  const int mode = choiceIndex(raw(Parameter::FilterMode), 3);
  if (mode != targetMode_) { modeFrom_ = mode_; targetMode_ = mode; modeRemaining_ = 480; }
  logBaseTarget_ = std::log2(physicalValue(Parameter::FilterFrequency, raw(Parameter::FilterFrequency)));
  qTarget_ = physicalValue(Parameter::FilterQ, raw(Parameter::FilterQ));
  envDepthTarget_ = physicalValue(Parameter::FilterEnv, raw(Parameter::FilterEnv));
  dryFilter_.set(choiceIndex(raw(Parameter::DryFilter), 2));
  open_.set(raw(Parameter::FilterFrequency) == 1 && raw(Parameter::FilterEnv) == .5f && raw(Parameter::FilterQ) == 0 ? 1 : 0);
  detector_.setSensitivity(raw(Parameter::TriggerSensitivity));
  envelope_.setTimes(physicalValue(Parameter::FilterAttack, raw(Parameter::FilterAttack)),
                     physicalValue(Parameter::FilterDecay, raw(Parameter::FilterDecay)));
  const bool drySpace = choiceIndex(raw(Parameter::DryDetune), 2);
  spread_[0].setAmount(drySpace ? raw(Parameter::Spread) : 0);
  for (std::size_t v = 1; v < spread_.size(); ++v) spread_[v].setAmount(raw(Parameter::Spread));
  chorus_[0].setDepth(drySpace ? raw(Parameter::Detune) : 0);
  chorus_[1].setDepth(raw(Parameter::Detune)); chorus_[2].setDepth(raw(Parameter::Detune));
}
void Pog3VoiceStages::prepare() {
  for (auto& spread : spread_) spread.prepare();
  constexpr std::array<std::size_t, 3> voices{0, 4, 5};
  for (std::size_t slot = 0; slot < chorus_.size(); ++slot) chorus_[slot].prepare(voices[slot]);
  prepared_ = true; reset();
}
void Pog3VoiceStages::reset() noexcept {
  updateTargets();
  inputGain_.reset(physicalValue(Parameter::InputGain, values_[index(Parameter::InputGain)]));
  master_.reset(physicalValue(Parameter::MasterLevel, values_[index(Parameter::MasterLevel)]));
  generatedGain_.reset(generatedGainTarget_);
  for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
    level_[voice].reset(values_[index(Parameter::DryLevel) + voice]);
    // Complete the current target explicitly, retaining center/edge endpoint exactness.
    const float p = values_[index(Parameter::DryPan) + voice];
    panLeft_[voice].reset(p == .5f ? 1 : p == 1 ? 0 : 1.41421356237f * std::cos(p * static_cast<float>(kPi / 2)));
    panRight_[voice].reset(p == .5f ? 1 : p == 0 ? 0 : 1.41421356237f * std::sin(p * static_cast<float>(kPi / 2)));
  }
  mode_ = {}; mode_[targetMode_] = 1; modeFrom_ = mode_; modeRemaining_ = 0;
  dryFilter_.reset(choiceIndex(values_[index(Parameter::DryFilter)], 2));
  open_.reset(values_[index(Parameter::FilterFrequency)] == 1 && values_[index(Parameter::FilterEnv)] == .5f
              && values_[index(Parameter::FilterQ)] == 0 ? 1 : 0);
  detector_.reset(); envelope_.reset();
  logBase_ = logBaseTarget_; q_ = qTarget_; envDepth_ = envDepthTarget_;
  cutoff_ = std::exp2(logBase_); g_ = std::tan(static_cast<float>(kPi) * cutoff_ / kSampleRate); damping_ = 1 / q_;
  gStep_ = dampingStep_ = 0; coefficientTick_ = recoveries_ = 0;
  for (auto& pair : filters_) for (auto& filter : pair) { filter.Reset(); filter.SetG(g_); filter.SetQ(q_); }
  for (auto& spread : spread_) spread.reset();
  for (auto& chorus : chorus_) chorus.reset();
}
PitchStereo Pog3VoiceStages::gainInput(PitchStereo input) noexcept {
  const float gain = inputGain_.tick();
  if (!std::isfinite(input.left)) { input.left = 0; ++recoveries_; }
  if (!std::isfinite(input.right)) { input.right = 0; ++recoveries_; }
  input = {gain * input.left, gain * input.right};
  if (!std::isfinite(input.left)) { input.left = 0; ++recoveries_; }
  if (!std::isfinite(input.right)) { input.right = 0; ++recoveries_; }
  return input;
}
PitchStereo Pog3VoiceStages::pan(PitchStereo input, std::size_t voice) noexcept {
  const float left = panLeft_[voice].tick(), right = panRight_[voice].tick(), level = level_[voice].tick();
  if (left == 1 && right == 1) return {input.left * level, input.right * level};
  const float mid = .5f * input.left + .5f * input.right;
  const float side = (.5f * input.left - .5f * input.right) * std::min(left, right);
  return {(left * mid + side) * level, (right * mid - side) * level};
}
PitchStereo Pog3VoiceStages::filter(PitchStereo input, std::size_t bus) noexcept {
  PitchStereo result{};
  const std::array<float, 2> source{input.left, input.right};
  for (std::size_t side = 0; side < 2; ++side) {
    auto& svf = filters_[bus][side];
    const float x = std::isfinite(source[side]) ? source[side] : 0;
    svf.SetG(g_); svf.SetQ(1 / damping_); svf.Process(x);
    bool valid = std::isfinite(source[side]) && std::isfinite(svf.lp()) && std::isfinite(svf.bp()) && std::isfinite(svf.hp());
    float value = 0;
    if (valid) {
      const float lp = open_.value() == 1 ? x : (1 - open_.value()) * svf.lp() + open_.value() * x;
      value = mode_[0] * lp + mode_[1] * (damping_ * svf.bp()) + mode_[2] * svf.hp();
      valid = std::isfinite(value);
    }
    if (!valid) { svf.Reset(); ++recoveries_; value = 0; }
    (side ? result.right : result.left) = value;
  }
  return result;
}
VoiceStageOutput Pog3VoiceStages::process(PitchStereo detectorSource, PitchStereo dry, const PitchVoices& voices) noexcept {
  if (!prepared_) return {};
  if (detector_.process(detectorSource)) envelope_.trigger();
  envelope_.process();
  if (coefficientTick_++ % 16 == 0) {
    logBase_ += kControlSmooth * (logBaseTarget_ - logBase_);
    q_ += kControlSmooth * (qTarget_ - q_);
    envDepth_ += kControlSmooth * (envDepthTarget_ - envDepth_);
    cutoff_ = std::clamp(std::exp2(logBase_ + envDepth_ * envelope_.value()), 40.0f, 20000.0f);
    const float targetG = std::tan(static_cast<float>(kPi) * cutoff_ / kSampleRate);
    gStep_ = (targetG - g_) / 16; dampingStep_ = (1 / q_ - damping_) / 16;
  }
  g_ = std::max(0.0f, g_ + gStep_); damping_ = std::clamp(damping_ + dampingStep_, .125f, 1 / kMinimumQ);
  if (modeRemaining_) {
    const float mix = static_cast<float>(481 - modeRemaining_) / 480;
    for (std::size_t m = 0; m < mode_.size(); ++m) mode_[m] = (1 - mix) * modeFrom_[m] + mix * (m == static_cast<std::size_t>(targetMode_) ? 1 : 0);
    --modeRemaining_;
  }
  open_.tick(); dryFilter_.tick();
  PitchStereo wet{};
  std::array<bool, 2> fault{};
  constexpr std::array<int, kVoiceCount> chorusSlot{0, -1, -1, -1, 1, 2};
  constexpr std::array<int, kVoiceCount> spreadSlot{0, -1, -1, 1, 2, 3};
  for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
    auto value = voice ? voices[voice] : dry;
    const auto clean = [&] {
      if (!std::isfinite(value.left)) { value.left = 0; ++recoveries_; fault[voice ? 0 : 1] = true; }
      if (!std::isfinite(value.right)) { value.right = 0; ++recoveries_; fault[voice ? 0 : 1] = true; }
    };
    clean(); value = pan(value, voice); clean();
    if (chorusSlot[voice] >= 0) value = chorus_[chorusSlot[voice]].process(value);
    if (spreadSlot[voice] >= 0) value = spread_[spreadSlot[voice]].process(value);
    if (!voice) dry = value;
    else { wet.left += value.left; wet.right += value.right; }
  }
  for (std::size_t bus = 0; bus < fault.size(); ++bus)
    if (fault[bus]) for (auto& svf : filters_[bus]) svf.Reset();
  wet = filter(wet, 0);
  const auto filteredDry = filter(dry, 1);
  const float blend = dryFilter_.value();
  if (blend == 1) dry = filteredDry;
  else if (blend != 0) dry = {(1 - blend) * dry.left + blend * filteredDry.left,
                             (1 - blend) * dry.right + blend * filteredDry.right};
  // Volume expression scales the generated bus after its filter, so ringing
  // cannot leak through a settled zero-gain endpoint. Dry remains independent.
  const float generatedGain = generatedGain_.tick();
  if (generatedGain == 0) wet = {};
  else if (generatedGain != 1) { wet.left *= generatedGain; wet.right *= generatedGain; }
  const float master = master_.tick();
  if (master == 0) return {};
  VoiceStageOutput result{{master * (dry.left + wet.left), master * (dry.right + wet.right)},
                         {master * wet.left, master * wet.right}, {master * dry.left, master * dry.right}};
  const auto recover = [&](float& mixed, float& generated, float& direct, std::size_t side) {
    if (std::isfinite(mixed) && std::isfinite(generated) && std::isfinite(direct)) return;
    mixed = generated = direct = 0; ++recoveries_;
    for (auto& pair : filters_) pair[side].Reset();
  };
  recover(result.mixed.left, result.wet.left, result.dry.left, 0);
  recover(result.mixed.right, result.wet.right, result.dry.right, 1);
  return result;
}
float Pog3VoiceStages::largestChorusStep() const noexcept {
  float step = 0;
  for (const auto& chorus : chorus_) step = std::max(step, chorus.largestReadStep());
  return step;
}

void Pog3SignalPath::prepare() {
  setSoundValues(values_); bank_.prepare(); stages_.prepare(); prepared_ = true; reset();
}
bool Pog3SignalPath::setSoundValues(const Values& values) noexcept {
  if (!stages_.setValues(values)) return false;
  values_ = values;
  for (auto& value : values_) value = unit(value);
  bank_.setFocus(choiceIndex(values_[index(Parameter::Focus)], 2));
  bank_.setAttackSeconds(physicalValue(Parameter::Attack, values_[index(Parameter::Attack)]));
  return true;
}
void Pog3SignalPath::reset() noexcept {
  bank_.reset(); stages_.reset();
  dry_.reset(choiceIndex(values_[index(Parameter::DryAttack)], 2) && bank_.attackSeconds() > 0);
}
VoiceStageOutput Pog3SignalPath::process(PitchStereo input) noexcept {
  if (!prepared_) return {};
  input = stages_.gainInput(input);
  const auto voices = bank_.process(input);
  const bool dryAttack = choiceIndex(values_[index(Parameter::DryAttack)], 2) && bank_.attackSeconds() > 0;
  const auto dry = dry_.process(input, voices[0], dryAttack);
  return stages_.process(input, dry, voices);
}
} // namespace ardor::pog3
