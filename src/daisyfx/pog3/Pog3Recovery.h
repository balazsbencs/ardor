#pragma once

#include <cstdint>

namespace ardor::pog3 {
enum class RecoveryState : std::uint8_t { Healthy, Waiting, Restarting, Bypassed };

struct RecoverySnapshot {
  std::uint64_t faults = 0;
  std::uint64_t recoveries = 0;
  RecoveryState state = RecoveryState::Healthy;
};
} // namespace ardor::pog3
