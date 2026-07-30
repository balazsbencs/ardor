#pragma once

namespace ardor {

enum class NamInputMode {
  Sum,
  Left,
  Right
};

inline float routeNamInput(NamInputMode mode, float left, float right) noexcept
{
  if (mode == NamInputMode::Left) return left;
  if (mode == NamInputMode::Right) return right;
  return (left + right) * 0.5f;
}

} // namespace ardor
