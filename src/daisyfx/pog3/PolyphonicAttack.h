#pragma once

#include "daisyfx/pog3/AttackBirthSlots.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ardor::pog3 {

class PitchFrame;
inline constexpr std::size_t kMaxPitchPartials = 256;
using PartialGains = std::array<float, kMaxPitchPartials>;
using StereoAttackGains = std::array<PartialGains, 2>;

// Audio-owned, fixed-capacity spectral excitation decomposition. Stereo
// magnitudes share event identities; complex audio is never summed here.
// A rise adds a new excitation without resetting the existing sustain.
class PolyphonicAttack {
public:
  static constexpr std::size_t kResolutions = 3, kFamilies = 16;
  void reset() noexcept;
  bool setSeconds(float seconds) noexcept;
  float seconds() const noexcept { return seconds_; }
  StereoAttackGains update(const PitchFrame& left, const PitchFrame& right,
                           std::size_t resolution, std::int64_t inputEnd) noexcept;
  std::size_t capacityEvents() const noexcept { return capacityEvents_; }
  std::size_t familyCount() const noexcept;

private:
  struct Excitation { float amplitude = 0; std::int64_t onset = 0; };
  struct Partial {
    std::uint64_t generation = 0, family = 0;
    std::int64_t seen = 0, onset = 0;
    float frequency = 0, magnitude = 0, sustain = 0;
    std::array<Excitation, 4> excitation{};
  };
  struct Binding { std::uint64_t source = 0, generation = 0; std::size_t slot = 0; };
  struct Family {
    std::uint64_t id = 0;
    float frequency = 0;
    std::int64_t onset = 0, seen = 0;
  };
  struct Observation {
    float frequency = 0, magnitude = 0;
    std::array<std::size_t, 2> track{kMaxPitchPartials, kMaxPitchPartials};
    std::array<std::uint64_t, 2> generation{};
  };
  struct Candidate { float frequency = 0, score = 0; std::size_t partial = 0; };
  struct FrequencyIndex { float frequency = 0; std::uint16_t slot = 0; };
  void indexCanonical(std::size_t resolution) noexcept;
  Family* owner(float frequency, std::int64_t inputEnd) noexcept;
  void group(std::size_t count, std::int64_t inputEnd, std::int64_t onset) noexcept;
  float envelope(Partial& partial, float magnitude, std::int64_t center,
                 std::int64_t inputEnd, std::int64_t onset, bool birth) noexcept;
  float gainAt(const Partial& partial, std::int64_t center) const noexcept;
  std::array<std::array<Partial, kMaxPitchPartials>, kResolutions> partials_{};
  std::array<std::array<std::array<Binding, kMaxPitchPartials>, 2>, kResolutions> bindings_{};
  std::array<Family, kFamilies> families_{};
  // Workspaces are members rather than large automatic audio-thread arrays.
  std::array<Observation, 2 * kMaxPitchPartials> observations_{};
  std::array<Candidate, 64> candidates_{};
  std::array<bool, kMaxPitchPartials> used_{};
  std::array<bool, kMaxPitchPartials> reserved_{};
  detail::AttackBirthSlots<kMaxPitchPartials> birthSlots_;
  std::array<bool, kMaxPitchPartials> rightUsed_{};
  std::array<FrequencyIndex, kMaxPitchPartials> rightFrequencies_{};
  // Only low/primary histories are canonical; short history never owns them.
  std::array<std::array<FrequencyIndex, kMaxPitchPartials>, 2> canonicalFrequencies_{};
  std::array<std::size_t, 2> canonicalCounts_{};
  float seconds_ = 0;
  std::uint64_t generation_ = 0;
  std::size_t capacityEvents_ = 0;
};

} // namespace ardor::pog3
