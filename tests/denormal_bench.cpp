// Measures the cost of subnormal (denormal) float arithmetic with and without
// flush-to-zero, to decide whether the audio thread needs FTZ on this CPU.
// Build: c++ -O2 -std=c++20 -o denormal_bench denormal_bench.cpp
// Run:   ./denormal_bench   (prints ns/op for each case and a verdict)
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#if defined(__aarch64__)
static void setFlushToZero(bool on) {
  std::uint64_t fpcr;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  if (on) {
    fpcr |= (1ull << 24);  // FZ
  } else {
    fpcr &= ~(1ull << 24);
  }
  asm volatile("msr fpcr, %0" ::"r"(fpcr));
}
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
static void setFlushToZero(bool on) {
  _MM_SET_FLUSH_ZERO_MODE(on ? _MM_FLUSH_ZERO_ON : _MM_FLUSH_ZERO_OFF);
  _MM_SET_DENORMALS_ZERO_MODE(on ? _MM_DENORMALS_ZERO_ON : _MM_DENORMALS_ZERO_OFF);
}
#else
static void setFlushToZero(bool) {}
#endif

namespace {

constexpr int kIterations = 50'000'000;

// One-pole feedback decay: the classic way reverb/delay tails and gain
// smoothers reach the subnormal range and stay there.
float decayLoop(float seed) {
  volatile float sink = 0.0f;
  float y = seed;
  for (int i = 0; i < kIterations; ++i) {
    y = y * 0.999f + 0.0f;
    sink = y;
  }
  return sink;
}

double timeNsPerOp(float seed) {
  const auto start = std::chrono::steady_clock::now();
  const float result = decayLoop(seed);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  (void)result;
  return std::chrono::duration<double, std::nano>(elapsed).count() / kIterations;
}

}  // namespace

int main() {
  // Warm up frequency scaling.
  timeNsPerOp(1.0f);

  // Seed 1.0 decays to subnormal after ~100k iterations, so ~99.8% of the
  // loop runs on subnormal values. Seed at FLT_MIN*4 gets there immediately.
  const float subnormalSeed = 4.7e-38f;  // just above FLT_MIN, decays into subnormals fast

  setFlushToZero(false);
  const double normalNs = timeNsPerOp(1.0e10f);  // stays in normal range longest
  const double denormNs = timeNsPerOp(subnormalSeed);

  setFlushToZero(true);
  const double ftzNs = timeNsPerOp(subnormalSeed);
  setFlushToZero(false);

  std::printf("normal operands:        %6.2f ns/op\n", normalNs);
  std::printf("subnormal operands:     %6.2f ns/op\n", denormNs);
  std::printf("subnormal with FTZ on:  %6.2f ns/op\n", ftzNs);

  const double penalty = denormNs / normalNs;
  std::printf("penalty factor: %.2fx\n", penalty);
  if (penalty > 1.5) {
    std::printf("VERDICT: denormals ARE a problem on this CPU — set FTZ on the audio thread.\n");
    return 1;
  }
  std::printf("VERDICT: denormals handled at (near) full speed on this CPU.\n");
  return 0;
}
