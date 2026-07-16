#pragma once

// Scoped floating-point environment guard for block-level DSP work. miniaudio
// already handles x86 callbacks, but offline rendering and AArch64 need the
// same guarantee explicitly. The previous per-thread mode is restored when
// the block completes so callers retain ownership of their FP environment.
#if defined(__aarch64__)
#include <cstdint>
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace ardor {

inline bool flushToZeroEnabled()
{
#if defined(__aarch64__)
  uint64_t fpcr = 0;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  return (fpcr & (uint64_t{1} << 24)) != 0;
#elif defined(__x86_64__) || defined(_M_X64)
  const unsigned int mxcsr = _mm_getcsr();
  return (mxcsr & _MM_FLUSH_ZERO_MASK) != 0 && (mxcsr & _MM_DENORMALS_ZERO_MASK) != 0;
#else
  return true;
#endif
}

class ScopedDenormalGuard {
public:
  ScopedDenormalGuard()
  {
#if defined(__aarch64__)
    asm volatile("mrs %0, fpcr" : "=r"(saved_));
    const uint64_t configured = saved_ | (uint64_t{1} << 24); // FPCR.FZ
    asm volatile("msr fpcr, %0" : : "r"(configured));
#elif defined(__x86_64__) || defined(_M_X64)
    saved_ = _mm_getcsr();
    _mm_setcsr(saved_ | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
#endif
  }

  ~ScopedDenormalGuard()
  {
#if defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(saved_);
#endif
  }

  ScopedDenormalGuard(const ScopedDenormalGuard&) = delete;
  ScopedDenormalGuard& operator=(const ScopedDenormalGuard&) = delete;

private:
#if defined(__aarch64__)
  uint64_t saved_ = 0;
#elif defined(__x86_64__) || defined(_M_X64)
  unsigned int saved_ = 0;
#endif
};

} // namespace ardor
