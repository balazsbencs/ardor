#pragma once

#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include "daisyfx/hosted/dsp/delay_tap_transition.h"
#include "daisyfx/hosted/dsp/svf.h"

namespace ardor::pog3 {

class Slew {
public:
  void set(float target, std::size_t samples = 480) noexcept;
  void reset(float value) noexcept { current_ = target_ = value; remaining_ = 0; }
  float tick() noexcept;
  float value() const noexcept { return current_; }
private:
  float current_ = 0, target_ = 0, step_ = 0;
  std::size_t remaining_ = 0;
};

// Linked, noncancelling playing-event detector. A retained peak reference
// suppresses low-note cycle chatter while fast/slow contrast admits re-plucks.
class PlayingDetector {
public:
  bool setSensitivity(float normalized) noexcept;
  void reset() noexcept;
  bool process(PitchStereo input) noexcept;
  std::size_t triggers() const noexcept { return triggers_; }
private:
  double fast_ = 0, slow_ = 0, reference_ = 0, framePeak_ = 0, gate_ = 1e-5;
  double blockPower_ = 0, referencePower_ = 0;
  float contrast_ = .44f;
  std::size_t refractory_ = 720, frame_ = 0, powerFrame_ = 0, triggers_ = 0;
  bool armed_ = true, quiet_ = true;
};

class FilterAd {
public:
  enum class State { Idle, Attack, Decay };
  bool setTimes(float attackSeconds, float decaySeconds) noexcept;
  void reset() noexcept;
  void trigger() noexcept;
  float process() noexcept;
  float value() const noexcept { return value_; }
  State state() const noexcept { return state_; }
private:
  State state_ = State::Idle;
  float value_ = 0, start_ = 0;
  std::size_t attack_ = 240, decay_ = 960, duration_ = 0, elapsed_ = 0;
};

class FractionalHistory {
public:
  void prepare(std::size_t maximumDelay);
  void reset() noexcept;
  void write(float sample) noexcept;
  float read(float delay) const noexcept;
  void advance() noexcept;
private:
  std::vector<float> samples_;
  std::size_t write_ = 0, maximum_ = 0;
};

// Both sides share one stationary-tap transition: anchors remain exactly 1:3.
class StereoSpread {
public:
  void prepare();
  bool setAmount(float normalized) noexcept;
  void reset() noexcept;
  PitchStereo process(PitchStereo input) noexcept;
  float rightTarget() const noexcept { return target_; }
  float rightAnchor() const noexcept { return taps_.to(); }
  bool transitioning() const noexcept { return taps_.active(); }
private:
  std::array<FractionalHistory, 2> history_;
  pedal::DelayTapTransition taps_{480};
  float target_ = 0;
  bool prepared_ = false;
};

class StereoDoubling {
public:
  void prepare(std::size_t voice);
  bool setDepth(float normalized) noexcept;
  void reset() noexcept;
  PitchStereo process(PitchStereo input) noexcept;
  float largestReadStep() const noexcept { return largestStep_; }
private:
  std::array<FractionalHistory, 2> history_;
  std::array<double, 2> phase_{};
  std::array<float, 2> read_{384, 384}, targetRead_{384, 384};
  Slew depth_;
  float target_ = 0, largestStep_ = 0;
  std::size_t voice_ = 0, tick_ = 0;
  bool prepared_ = false;
};

struct VoiceStageOutput { PitchStereo mixed{}, wet{}, dry{}; };

// Audio-owned sound controls only. Expression ownership is resolved by the
// processor before setValues; this class never parses JSON or publishes
// effective values back into the control targets. No allocations after prepare.
class Pog3VoiceStages {
public:
  Pog3VoiceStages() noexcept;
  void prepare();
  void reset() noexcept;
  bool setValues(const Values& values) noexcept;
  bool setGeneratedGain(float normalized) noexcept;
  PitchStereo gainInput(PitchStereo input) noexcept;
  VoiceStageOutput process(PitchStereo detectorSource, PitchStereo dry,
                           const PitchVoices& voices) noexcept;
  float filterCutoff() const noexcept { return cutoff_; }
  float filterEnvelope() const noexcept { return envelope_.value(); }
  std::size_t triggers() const noexcept { return detector_.triggers(); }
  std::size_t recoveries() const noexcept { return recoveries_; }
  float largestChorusStep() const noexcept;
private:
  void updateTargets() noexcept;
  PitchStereo pan(PitchStereo input, std::size_t voice) noexcept;
  PitchStereo filter(PitchStereo input, std::size_t bus) noexcept;
  Values values_{};
  std::array<Slew, kVoiceCount> level_, panLeft_, panRight_;
  Slew inputGain_, master_, dryFilter_, open_, generatedGain_;
  float generatedGainTarget_ = 1;
  std::array<float, 3> mode_{1, 0, 0}, modeFrom_{1, 0, 0};
  int targetMode_ = 0;
  std::size_t modeRemaining_ = 0;
  std::array<StereoSpread, 4> spread_;
  std::array<StereoDoubling, 3> chorus_;
  std::array<std::array<pedal::Svf, 2>, 2> filters_;
  PlayingDetector detector_;
  FilterAd envelope_;
  float logBase_ = 0, logBaseTarget_ = 0, envDepth_ = 0, envDepthTarget_ = 0;
  float q_ = .70710678f, qTarget_ = .70710678f, cutoff_ = 20000;
  float g_ = 0, damping_ = 1.41421356f, gStep_ = 0, dampingStep_ = 0;
  std::size_t coefficientTick_ = 0, recoveries_ = 0;
  bool prepared_ = false;
};

// Sound-path composition beneath the expression controller. Input gain is
// applied once before analysis/onsets, Master once after dry/generated stages.
// Accepts effective sound values and scalar gain/Warp, without interpreting modes.
class Pog3SignalPath {
public:
  void prepare();
  void reset() noexcept;
  bool setSoundValues(const Values& values) noexcept;
  bool setWarp(float normalized) noexcept { return bank_.setWarp(normalized); }
  bool setFreeze(ExpressionMode mode, float position, bool dryEligible) noexcept { return bank_.setFreeze(mode, position, dryEligible); }
  const SpectralFreeze& freeze() const noexcept { return bank_.freeze(); }
  bool setGeneratedGain(float normalized) noexcept { return stages_.setGeneratedGain(normalized); }
  VoiceStageOutput process(PitchStereo input) noexcept;
  bool healthy() const noexcept { return bank_.healthy() && stages_.recoveries() == 0; }
  std::size_t transformCount() const noexcept { return bank_.transformCount(); }
  std::size_t deadlineMisses() const noexcept { return bank_.deadlineMisses(); }
  const Pog3VoiceStages& stages() const noexcept { return stages_; }
private:
  Values values_ = defaultValues();
  PolyphonicPitchBank bank_;
  Pog3VoiceStages stages_;
  DryAttackRouter dry_;
  bool prepared_ = false;
};

} // namespace ardor::pog3
