#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ardor::pog3::detail {

// One lazy snapshot per frame, after aging. Only selected tracks change before
// publication, and used flags only become true. Keep ascending indices within
// the combined empty/expired tier, then missed ages 4, 3, 2, 1. Generated age
// zero is excluded by the original fallback's strict missed > oldest predicate.
template<std::size_t Capacity>
class FrameBirthSlots {
public:
  static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max());
  template<class Track>
  void prepare(const std::array<Track, Capacity>& tracks) noexcept {
    std::array<std::uint16_t, 5> counts{};
    for (const auto& track : tracks) {
      const auto bucket = tier(track);
      if (bucket < counts.size()) ++counts[bucket];
    }
    std::uint16_t offset = 0;
    for (std::size_t bucket = 0; bucket < counts.size(); ++bucket) {
      next_[bucket] = offset;
      offset += counts[bucket];
      end_[bucket] = offset;
    }
    auto write = next_;
    for (std::size_t slot = 0; slot < tracks.size(); ++slot) {
      const auto bucket = tier(tracks[slot]);
      if (bucket < counts.size()) slots_[write[bucket]++] = static_cast<std::uint16_t>(slot);
    }
  }
  std::size_t take(const std::array<bool, Capacity>& used) noexcept {
    for (std::size_t bucket = 0; bucket < next_.size(); ++bucket) {
      while (next_[bucket] < end_[bucket] && used[slots_[next_[bucket]]]) ++next_[bucket];
      if (next_[bucket] < end_[bucket]) return slots_[next_[bucket]++];
    }
    return Capacity;
  }
private:
  template<class Track>
  static std::size_t tier(const Track& track) noexcept {
    if (!track.generation || track.missed > 4) return 0;
    return 5 - track.missed;
  }
  std::array<std::uint16_t, Capacity> slots_{};
  std::array<std::uint16_t, 5> next_{}, end_{};
};

} // namespace ardor::pog3::detail
