// SPDX-License-Identifier: MIT
// Adapted from phillipmself/NeuralAmpModelerCoreParametric; see README.md.
#pragma once

#include <atomic>
#ifndef NDEBUG
#include <cassert>
#endif

namespace nam
{

/// \brief Debug-only reentrancy check for a parametric model's control/process API.
///
/// Asserts on construction that no other guarded call on the same flag is in flight, and
/// clears it on destruction (including when unwinding through an exception). In a release
/// build this is an empty no-op, so call sites need no #ifndef NDEBUG of their own.
#ifndef NDEBUG
class DebugParamApiGuard
{
public:
  explicit DebugParamApiGuard(std::atomic_flag& flag) : _flag(flag)
  {
    assert(!_flag.test_and_set(std::memory_order_acquire));
  }
  ~DebugParamApiGuard() { _flag.clear(std::memory_order_release); }

  DebugParamApiGuard(const DebugParamApiGuard&) = delete;
  DebugParamApiGuard& operator=(const DebugParamApiGuard&) = delete;

private:
  std::atomic_flag& _flag;
};
#else
class DebugParamApiGuard
{
public:
  explicit DebugParamApiGuard(std::atomic_flag&) {}
};
#endif

} // namespace nam
