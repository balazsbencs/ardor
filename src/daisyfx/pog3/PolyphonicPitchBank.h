#pragma once

#include "daisyfx/pog3/Pog3Parameters.h"
#include "daisyfx/pog3/PolyphonicAttack.h"
#include "daisyfx/pog3/SpectralFrameStream.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ardor::pog3 {

struct PitchRegion {
  std::size_t bin = 0, first = 0, last = 0, track = 0;
  std::uint64_t generation = 0;
  float frequencyBins = 0;
  float magnitude = 0;
};

// Off-thread preparation. The interpolation table is shared by all renderers
// of a resolution, alongside the immutable FFT/window plan.
class PitchPlan {
public:
  static constexpr int kRadius = 12;
  static constexpr std::size_t kPhases = 512;
  using Weights = std::array<float, 2 * kRadius>;
  explicit PitchPlan(std::shared_ptr<const SpectralPlan> spectral);
  float lobe(float distance) const noexcept;
  const std::shared_ptr<const SpectralPlan> spectral;
  const Weights& interpolationWeights(std::size_t phase) const noexcept {
    return interpolation_[phase > kPhases ? kPhases : phase];
  }

private:
  std::array<Weights, kPhases + 1> interpolation_{};
  std::array<float, 16 * kPhases + 1> hannLobe_{};
};

// One interpretation of one channel's frame, shared across its output voices.
// Peak identities survive bin changes; capacities and association work are fixed.
class PitchFrame {
public:
  PitchFrame() = default;
  PitchFrame(const PitchFrame&) = delete;
  PitchFrame& operator=(const PitchFrame&) = delete;
  void prepare(std::shared_ptr<const PitchPlan> plan, float frequencyCeiling = kSampleRate / 2);
  void reset() noexcept;
  void update(std::span<const std::complex<float>> spectrum) noexcept;
  std::span<const PitchRegion> regions() const noexcept { return {regions_.data(), count_}; }
  std::span<const std::complex<float>> spectrum() const noexcept { return spectrum_; }
  std::size_t capacityEvents() const noexcept { return capacityEvents_; }
  std::int64_t centerSamples() const noexcept { return centerSamples_; }
  std::size_t frameSize() const noexcept { return plan_ ? plan_->spectral->frameSize() : 0; }
  float lobe(float distance) const noexcept { return plan_->lobe(distance); }

private:
  struct Track { float frequency = 0, velocity = 0; unsigned missed = 0; std::uint64_t generation = 0; };
  std::shared_ptr<const PitchPlan> plan_;
  std::vector<float> phase_, magnitude_, previousMagnitude_;
  std::vector<PitchRegion> candidates_;
  std::array<PitchRegion, kMaxPitchPartials> regions_{};
  std::array<Track, kMaxPitchPartials> tracks_{};
  std::span<const std::complex<float>> spectrum_;
  std::size_t count_ = 0, capacityEvents_ = 0;
  std::uint64_t generation_ = 0;
  bool previous_ = false;
  std::int64_t centerSamples_ = 0, samples_ = 0;
  std::size_t peakLimit_ = 0;
};

class PitchRenderer {
public:
  void prepare(std::shared_ptr<const PitchPlan> plan);
  void reset() noexcept;
  float pop() noexcept { return synthesis_.pop(); }
  // The optional 4096-resolution frame supplies resolved low-band partials
  // while the primary frame retains high-band regions/noise. No extra IFFT.
  bool render(const PitchFrame& frame, float semitones, const PitchFrame* lowAnalysis = nullptr,
              std::size_t startOffset = 0, const PartialGains* gains = nullptr,
              const PartialGains* lowGains = nullptr, bool partialProcessing = false) noexcept;

private:
  struct Phase { std::uint64_t generation = 0; double offset = 0; float frequency = 0, ratio = 1; unsigned age = 0; double alignment = 0; };
  std::shared_ptr<const PitchPlan> plan_;
  SpectralSynthesis synthesis_;
  std::vector<std::complex<float>> spectrum_;
  std::array<Phase, kMaxPitchPartials> phases_{};
  std::array<Phase, kMaxPitchPartials> lowPhases_{};
  std::array<std::complex<float>, kMaxPitchPartials> lowCarriers_{};
  bool shifted_ = false;
};

struct PitchStereo { float left = 0, right = 0; };
using PitchVoices = std::array<PitchStereo, kVoiceCount>;

// Separate dry route for the future processor. A reversible 20 ms fade keeps
// the immediate dry endpoint exact. The caller disables eligibility at Attack=0.
class DryAttackRouter {
public:
  void reset(bool enabled = false) noexcept { position_ = enabled ? 1 : 0; }
  PitchStereo process(PitchStereo input, PitchStereo processed, bool enabled) noexcept {
    constexpr float step = 1.0f / 960;
    position_ = enabled ? std::min(1.0f, position_ + step) : std::max(0.0f, position_ - step);
    if (position_ == 0) return input;
    if (position_ == 1) return processed;
    return {(1 - position_) * input.left + position_ * processed.left,
            (1 - position_) * input.right + position_ * processed.right};
  }
  float position() const noexcept { return position_; }
private:
  float position_ = 0;
};

// DSP bank only: levels, pan, filter, space and expression routing are
// composed by the future processor. Voice zero is the N+H delayed unison path.
// Both Focus variants stay warm, including muted voices, so a reversal never
// exposes stale OLA. Account for the full transition transform count at all times.
class PolyphonicPitchBank {
public:
  PolyphonicPitchBank() = default;
  PolyphonicPitchBank(const PolyphonicPitchBank&) = delete;
  PolyphonicPitchBank& operator=(const PolyphonicPitchBank&) = delete;
  void prepare(); // 48 kHz; all allocations and planning occur here.
  void reset() noexcept; // Retain Focus/Warp targets, clear every audio history.
  // Audio-thread-owned setters; the processor publishes external controls via
  // ParameterTargets. These do not introduce a second cross-thread target bank.
  void setFocus(bool enabled) noexcept { focusTarget_ = enabled; }
  bool setWarp(float normalized) noexcept;
  bool setAttackSeconds(float seconds) noexcept { return attack_.setSeconds(seconds); }
  float attackSeconds() const noexcept { return attack_.seconds(); }
  PitchVoices process(PitchStereo input) noexcept;
  bool healthy() const noexcept { return healthy_; }
  float focusPosition() const noexcept { return focus_; }
  std::size_t capacityEvents() const noexcept;
  std::size_t transformCount() const noexcept { return transforms_; }
  std::size_t deadlineMisses() const noexcept { return deadlineMisses_; }
  std::size_t attackCapacityEvents() const noexcept { return attack_.capacityEvents(); }
  std::size_t attackFamilyCount() const noexcept { return attack_.familyCount(); }
  static constexpr std::size_t kLongDelay = 2048 + 256, kShortDelay = 1024 + 128;

private:
  std::array<SpectralAnalysis, 2> longAnalysis_, shortAnalysis_, lowAnalysis_;
  std::array<PitchFrame, 2> longFrames_, shortFrames_, lowFrames_;
  std::array<std::array<PitchRenderer, 2>, kVoiceCount> longVoices_;
  std::array<std::array<PitchRenderer, 2>, 2> shortVoices_;
  PolyphonicAttack attack_;
  StereoAttackGains longGains_{}, shortGains_{}, lowGains_{};
  std::int64_t inputSamples_ = 0;
  float warpTarget_ = 1, warp_ = 1, focus_ = 0;
  float frameWarp_ = 1;
  bool focusTarget_ = false, prepared_ = false, healthy_ = true;
  bool longAttackReady_ = true, shortAttackReady_ = true;
  bool longAttackActive_ = false;
  std::size_t transforms_ = 0;
  std::size_t longJob_ = 12, shortJob_ = 4, longAge_ = 0, shortAge_ = 0, deadlineMisses_ = 0;
};

} // namespace ardor::pog3
