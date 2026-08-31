#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace ardor {

// Fixed-capacity single-producer/single-consumer queue. The producer and
// consumer must each remain on one thread; neither operation blocks or
// allocates.
template <typename T, std::size_t Capacity>
class LooperSpscQueue {
  static_assert(Capacity > 0);
  static_assert(std::is_trivially_copyable_v<T>);

public:
  bool tryPush(const T& value) noexcept
  {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    if (write - read >= Capacity) return false;
    values_[write % Capacity] = value;
    write_.store(write + 1, std::memory_order_release);
    return true;
  }

  bool tryPop(T& value) noexcept
  {
    const auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    if (read == write) return false;
    value = values_[read % Capacity];
    read_.store(read + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(64) std::array<T, Capacity> values_ {};
  alignas(64) std::atomic<std::size_t> write_ {0};
  alignas(64) std::atomic<std::size_t> read_ {0};
};

} // namespace ardor
