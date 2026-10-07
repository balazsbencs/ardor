#pragma once

#include "erb_cadence_bank.h"
#include "daisyfx/pog3/PolyphonicPitchBank.h"

namespace pog3_trial {

inline float erbHighWeight(float hz) noexcept {
  const float t = std::clamp((hz - 200) / 100, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// Reuse the actual production analysis, peak identities, excitation ownership
// and freeze state machine. No inverse transforms or live spectral renderers.
// This intentionally prices all three warm resolutions, including short Focus.
class ErbOwnership {
public:
  using Frames = std::array<ardor::pog3::PitchFrame, 2>;
  using Gains = ardor::pog3::StereoAttackGains;
  ErbOwnership() {
    using namespace ardor::pog3;
    auto interpolation = std::make_shared<PitchInterpolation>();
    constexpr std::array<std::size_t, 3> sizes{2048, 1024, 4096}, hops{256, 128, 512};
    for (std::size_t r = 0; r < 3; ++r) {
      auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(sizes[r], hops[r]), interpolation);
      for (std::size_t c = 0; c < 2; ++c) {
        analysis_[r][c].prepare(plan->spectral, c == 1);
        frames_[r][c].prepare(plan, r == 2 ? 400 : 24000);
      }
    }
    reset();
  }
  void reset() noexcept {
    for (auto& pair : analysis_) for (auto& a : pair) a.reset();
    for (auto& pair : frames_) for (auto& f : pair) f.reset();
    for (auto& pair : gains_) for (auto& g : pair) g.fill(1);
    attack.reset(); freeze.reset(); samples_ = transforms_ = 0;
    pending_ = {}; ends_ = {}; freezePending_ = false;
  }
  // Return bits for gain updates. Their timestamps retain the original left
  // boundary despite deferred right transforms and staged short/freeze work.
  unsigned process(std::array<float, 2> input, bool runFreeze) noexcept {
    ++samples_;
    for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 2; ++c)
      if (analysis_[r][c].push(input[c])) {
        frames_[r][c].update(analysis_[r][c].spectrum()); ++transforms_;
        if (c == 0) { pending_[r] = true; ends_[r] = samples_; }
      }
    unsigned updated = 0;
    // Canonical low ownership must precede primary and then short ownership.
    for (const auto r : {2U, 0U, 1U}) {
      const std::size_t stage = r == 1 ? 8 : 1;
      if (pending_[r] && samples_ - ends_[r] >= stage
          && analysis_[r][0].frameCount() == analysis_[r][1].frameCount()) {
        gains_[r] = attack.update(frames_[r][0], frames_[r][1], r, ends_[r]);
        pending_[r] = false; updated |= 1U << r;
        if (r == 0) freezePending_ = true;
      }
    }
    if (freezePending_ && !pending_[0] && !pending_[1] && samples_ - ends_[0] >= 9) {
      if (runFreeze) freeze.update(frames_[0], frames_[1], frames_[2], gains_[0], gains_[1], gains_[2], ends_[0]);
      freezePending_ = false;
    }
    return updated;
  }
  const Frames& frames(std::size_t r) const noexcept { return frames_[r]; }
  const Gains& gains(std::size_t r) const noexcept { return gains_[r]; }
  std::size_t samples() const noexcept { return samples_; }
  std::size_t transforms() const noexcept { return transforms_; }
  std::size_t capacityEvents() const noexcept {
    std::size_t n = attack.capacityEvents() + freeze.capacityEvents();
    for (const auto& pair : frames_) for (const auto& f : pair) n += f.capacityEvents();
    return n;
  }
  ardor::pog3::PolyphonicAttack attack;
  ardor::pog3::SpectralFreeze freeze;
private:
  std::array<std::array<ardor::pog3::SpectralAnalysis, 2>, 3> analysis_;
  std::array<Frames, 3> frames_;
  std::array<Gains, 3> gains_{};
  std::array<bool, 3> pending_{};
  std::array<std::size_t, 3> ends_{};
  std::size_t samples_ = 0, transforms_ = 0;
  bool freezePending_ = false;
};

// Experimental projection: weight each note's gain by its predicted analytic
// band power, including the real-input positive AND negative frequency terms.
// One band still has only one envelope: coincident notes cannot be isolated.
// This is an audible/CPU prototype, not a replacement for independent-note DSP.
inline void erbMapAttack(const ErbOwnership& ownership,
                        const std::array<ErbPs2Reference::Design, 69>& design,
                        ErbCadenceBank<32, true>::AttackGains& result,
                        std::size_t main) noexcept {
  const auto group = main == 1 ? 1U : 0U;
  for (std::size_t c = 0; c < 2; ++c) {
    std::array<double, 69> power{}, audible{};
    for (const auto r : {main, std::size_t{2}}) {
      const auto& frame = ownership.frames(r)[c];
      for (const auto& p : frame.regions()) {
        const float hz = p.frequencyBins * 48000 / frame.frameSize();
        const double weight = r == 2 ? 1 - erbHighWeight(hz) : erbHighWeight(hz);
        if (weight == 0) continue;
        const auto z = std::polar(1.0, -2 * std::numbers::pi * hz / 48000);
        const double magnitude = p.magnitude * 2 / frame.frameSize();
        const double numerator = weight * magnitude * magnitude * std::norm(1.0 - z * z);
        const float gain = ownership.gains(r)[c][p.track];
        for (std::size_t k = 0; k < 69; ++k) {
          const auto& d = design[k];
          const double positive = std::norm(1.0 - d.pole * z);
          const double negative = std::norm(1.0 - d.pole * std::conj(z));
          const double predicted = numerator * d.numerator * d.numerator
            * (1 / (positive * positive) + 1 / (negative * negative));
          power[k] += predicted; audible[k] += predicted * gain;
        }
      }
    }
    for (std::size_t k = 0; k < 69; ++k)
      result[c][k][group] = power[k] > 1e-30 ? static_cast<float>(audible[k] / power[k]) : 1;
  }
}

// Fixed-capacity additive held carriers. Every 32 samples reanchor a double
// real recurrence; IDs survive target assignment, Warp and volume changes.
// Control/frequency events are quantized to the next endpoint. Live analysis
// and all eight ERB outputs remain warm throughout a hold and its release.
class ErbHeldRenderer {
public:
  using Frame = ErbCadenceBank<32, true>::Frame;
  using Ratios = ErbPhaseVoices::Ratios;
  void reset() noexcept { states_ = {}; counts_ = {}; previousCounts_ = {}; position_ = 0; }
  template<class Freeze>
  Frame process(const Freeze& freeze, const Ratios& ratios,
                std::size_t sample) noexcept {
    if (position_ == 0) refresh(freeze, ratios, sample);
    Frame out{};
    for (std::size_t v = 0; v < 8; ++v) for (std::size_t c = 0; c < 2; ++c)
      for (std::size_t a = 0; a < counts_[v][c]; ++a) {
        auto& s = states_[v][c][active_[v][c][a]];
        out[v][c] += static_cast<float>(s.amplitude * s.real);
        const double next = s.coefficient * s.real - s.previous;
        s.previous = s.real; s.real = next; s.amplitude += s.amplitudeStep;
      }
    position_ = (position_ + 1) & 31;
    return out;
  }
private:
  struct State {
    std::uint64_t id = 0;
    double phase = 0, step = 0, real = 0, previous = 0, coefficient = 0;
    float amplitude = 0, amplitudeStep = 0;
  };
  template<class Freeze>
  void refresh(const Freeze& freeze, const Ratios& ratios,
               std::size_t sample) noexcept {
    constexpr double tau = 2 * std::numbers::pi;
    const auto& table = erbPhaseTables();
    for (std::size_t v = 0; v < 8; ++v) for (std::size_t c = 0; c < 2; ++c) {
      counts_[v][c] = 0;
      for (const auto r : {v >= 6 ? 1U : 0U, 2U}) {
        const auto& band = freeze.band(r, c);
        const std::size_t offset = r == 2 ? 256 : 0;
        const auto previousCount = previousCounts_[v][c][r == 2];
        previousCounts_[v][c][r == 2] = band.count;
        for (std::size_t i = 0; i < std::max(previousCount, band.count); ++i) {
          auto& s = states_[v][c][offset + i];
          if (i >= band.count || !band.partials[i].id) { s = {}; continue; }
          const auto& p = band.partials[i];
          const double hz = p.frequency * ratios[v];
          if (s.id != p.id) {
            // Extrapolate at INPUT frequency before shifting: different source
            // window centers then seed the same physical phase at capture.
            // Matching the live ERB per-band phase offsets is still unresolved.
            s = {}; s.id = p.id;
            s.phase = std::remainder(p.phase + tau * p.frequency * (static_cast<double>(sample) - p.center) / 48000, tau);
          } else s.phase = erbWrapped(s.phase + s.step * 32);
          s.step = tau * hz / 48000;
          const float weight = r == 2 ? 1 - erbHighWeight(p.frequency) : erbHighWeight(p.frequency);
          const double edge = std::clamp((hz - 18000) / 2000, 0.0, 1.0);
          const double mask = hz >= 24000 ? 0 : .5 + .5 * std::cos(std::numbers::pi * edge);
          const float amplitude = 2 * p.magnitude * weight * mask;
          s.amplitudeStep = (amplitude - s.amplitude) / 32;
          if (hz >= 24000 || (amplitude == 0 && s.amplitude == 0)) {
            s.amplitude = s.amplitudeStep = 0; continue;
          }
          // Keep fading the previous amplitude if its crossover weight became
          // zero. Frequencies at/above Nyquist are muted immediately instead.
          s.real = table.cos(s.phase);
          s.previous = table.cos(erbWrapped(s.phase - s.step));
          s.coefficient = 2 * erbRecurrenceCos(erbWrapped(s.step));
          active_[v][c][counts_[v][c]++] = static_cast<std::uint16_t>(offset + i);
        }
      }
    }
  }
  std::array<std::array<std::array<State, 512>, 2>, 8> states_{};
  std::array<std::array<std::array<std::uint16_t, 512>, 2>, 8> active_{};
  std::array<std::array<std::size_t, 2>, 8> counts_{};
  std::array<std::array<std::array<std::size_t, 2>, 2>, 8> previousCounts_{};
  std::size_t position_ = 0;
};

class ErbAttackFreezeBank {
public:
  enum class Stage { Ownership, Attack, Freeze };
  using Frame = ErbCadenceBank<32, true>::Frame;
  explicit ErbAttackFreezeBank(Stage stage = Stage::Freeze) : stage_(stage) { reset(); }
  void reset() noexcept {
    bank_.reset(); ownership_.reset(); held_.reset(); mix_ = {}; gain_ = 1;
    for (auto& channel : gains_) for (auto& band : channel) band.fill(1);
  }
  void setAttack(float seconds) noexcept {
    ownership_.attack.setSeconds(seconds);
    if (ownership_.attack.seconds() == 0) for (auto& c : gains_) for (auto& b : c) b.fill(1);
  }
  void setFreeze(ardor::pog3::ExpressionMode mode, float position, bool dry = true) noexcept {
    ownership_.freeze.setControls(mode, position, dry);
  }
  Frame process(std::array<float, 2> input) noexcept {
    const auto updated = ownership_.process(input, stage_ == Stage::Freeze);
    if (stage_ != Stage::Ownership && ownership_.attack.seconds() > 0) {
      if (updated & 1) erbMapAttack(ownership_, bank_.design(), gains_, 0);
      if (updated & 2) erbMapAttack(ownership_, bank_.design(), gains_, 1);
    }
    // Use the same specialization when disabled: ARM contraction can otherwise
    // round a nominal multiply-by-one gain path differently from the raw bank.
    auto out = stage_ == Stage::Ownership || ownership_.attack.seconds() == 0
      ? bank_.process(input) : bank_.process<true>(input, &gains_);
    if (stage_ == Stage::Freeze) {
      // Ratios only change at external Warp control events.
      const auto frozen = held_.process(ownership_.freeze, ratios_, ownership_.samples() - 1);
      gain_ += std::clamp(ownership_.freeze.gain() - gain_, -1.0f / 960, 1.0f / 960);
      for (std::size_t v = 0; v < 8; ++v) {
        mix_[v] += std::clamp(ownership_.freeze.mix(v) - mix_[v], -1.0f / 960, 1.0f / 960);
        for (std::size_t c = 0; c < 2; ++c) out[v][c] = (1 - mix_[v]) * out[v][c] + mix_[v] * gain_ * frozen[v][c];
      }
    }
    return out;
  }
  void transpose(double extent) noexcept {
    extent_ = std::clamp(extent, 0.0, 1.0); bank_.setWarp(extent_);
    constexpr std::array<double, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
    for (std::size_t v = 0; v < 8; ++v) ratios_[v] = std::exp2(semitones[v] * (v >= 6 ? 1 : extent_) / 12);
  }
  const ErbOwnership& ownership() const noexcept { return ownership_; }
private:
  ErbCadenceBank<32, true> bank_;
  ErbOwnership ownership_;
  ErbHeldRenderer held_;
  ErbCadenceBank<32, true>::AttackGains gains_{};
  ErbPhaseVoices::Ratios ratios_{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  std::array<float, 8> mix_{};
  float gain_ = 1;
  double extent_ = 1;
  Stage stage_;
};
}
