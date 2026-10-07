#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ardor::pog3::detail {

// Snapshot available slot keys once per update. Empty slots retain scan order;
// generated slots sort only when needed. All storage belongs to the processor.
template<std::size_t Capacity>
class AttackBirthSlots {
public:
  static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max());
  void reset() noexcept {
    emptyCount_ = generatedCount_ = nextEmpty_ = nextGenerated_ = 0;
    sorted_ = false;
  }
  // Caller visits each unreserved, unused slot once, in ascending slot order.
  void add(std::size_t slot, std::uint64_t generation, std::int64_t seen) noexcept {
    if (!generation) entries_[emptyCount_++] = {0, static_cast<std::uint16_t>(slot)};
    // Preserve the original strict seen < INT64_MAX selection predicate.
    else if (seen < std::numeric_limits<std::int64_t>::max())
      entries_[Capacity - 1 - generatedCount_++] = {seen, static_cast<std::uint16_t>(slot)};
  }
  std::size_t take(const std::array<bool, Capacity>& used) noexcept {
    while (nextEmpty_ < emptyCount_ && used[entries_[nextEmpty_].slot]) ++nextEmpty_;
    if (nextEmpty_ < emptyCount_) return entries_[nextEmpty_++].slot;
    if (!sorted_) {
      nextGenerated_ = Capacity - generatedCount_;
      std::sort(entries_.begin() + nextGenerated_, entries_.end(), [](const auto& a, const auto& b) {
        return a.seen == b.seen ? a.slot < b.slot : a.seen < b.seen;
      });
      sorted_ = true;
    }
    while (nextGenerated_ < Capacity && used[entries_[nextGenerated_].slot]) ++nextGenerated_;
    return nextGenerated_ < Capacity ? entries_[nextGenerated_++].slot : Capacity;
  }
private:
  struct Entry { std::int64_t seen = 0; std::uint16_t slot = 0; };
  std::array<Entry, Capacity> entries_{};
  std::size_t emptyCount_ = 0, generatedCount_ = 0, nextEmpty_ = 0, nextGenerated_ = 0;
  bool sorted_ = false;
};

} // namespace ardor::pog3::detail
