#pragma once

#include "daisyfx/pog3/Pog3Parameters.h"
#include "daisyfx/pog3/PolyphonicAttack.h"

#include <array>
#include <cstdint>

namespace ardor::pog3 {
class PitchFrame;

struct FrozenPartial {
  std::int64_t center = 0;
  std::uint64_t id = 0, liveGeneration = 0;
  // Capture's complex<float> argument is already a float. Continuous renderer
  // phase accumulation remains double; only the original seed is compacted.
  float frequency = 0, magnitude = 0, phase = 0;
  std::uint16_t liveTrack = 0;
  bool referenceLeft = false;
};
struct FrozenBand {
  std::array<FrozenPartial, kMaxPitchPartials> partials{};
  std::size_t count = 0;
  std::int64_t center = 0;
};

// Audio-owned, fixed storage. Called once per primary frame after attack jobs,
// before renderer jobs. Held/target data never changes during those jobs.
class SpectralFreeze {
public:
  enum class State { Live, CapturePending, Held, Gliding, Releasing };
  void reset() noexcept;
  bool setControls(ExpressionMode mode, float position, bool dryEligible) noexcept;
  void update(const std::array<PitchFrame, 2>& primary,
              const std::array<PitchFrame, 2>& shortFrames,
              const std::array<PitchFrame, 2>& low,
              const StereoAttackGains& primaryGains,
              const StereoAttackGains& shortGains,
              const StereoAttackGains& lowGains, std::int64_t inputEnd) noexcept;
  const FrozenBand& band(std::size_t resolution, std::size_t channel) const noexcept { return held_[resolution][channel]; }
  float mix(std::size_t voice) const noexcept { return voice ? mix_ : mix_ * dryMix_; }
  float gain() const noexcept { return gain_; }
  State state() const noexcept { return state_; }
  bool latched() const noexcept { return latched_; }
  std::size_t captures() const noexcept { return captures_; }
  std::size_t targets() const noexcept { return targets_; }
  std::size_t capacityEvents() const noexcept { return capacityEvents_; }

private:
  using Bands = std::array<std::array<FrozenBand, 2>, 3>;
  struct Target { float frequency = 0, magnitude = 0; };
  struct TargetBand {
    std::array<Target, kMaxPitchPartials> partials{};
    std::size_t count = 0;
  };
  using Targets = std::array<std::array<TargetBand, 2>, 3>;
  static void capture(FrozenBand& result, const PitchFrame& source, const PartialGains& gains) noexcept;
  void beginCapture() noexcept;
  void assignTarget() noexcept;
  bool onset() const noexcept;
  Bands latest_{}, requested_{}, held_{};
  Targets previous_{}, goal_{};
  std::array<bool, kMaxPitchPartials> used_{};
  ExpressionMode mode_ = ExpressionMode::Off;
  State state_ = State::Live;
  float position_ = 0, mix_ = 0, dryMix_ = 0, gain_ = 1;
  bool heel_ = true, latched_ = false, dryEligible_ = false;
  bool ready_ = false, requestedValid_ = false, initial_ = false, targetPending_ = false;
  std::int64_t initialAudibleAt_ = -1, lastEvent_ = -1920, targetAt_ = 0;
  std::size_t remaining_ = 0, duration_ = 960;
  std::uint64_t serial_ = 0;
  std::size_t captures_ = 0, targets_ = 0, capacityEvents_ = 0;
};
} // namespace ardor::pog3
