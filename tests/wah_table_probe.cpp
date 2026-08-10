// Synthetic stand-in for the wah's runtime cost. Allocates a table of the
// planned size and performs the planned per-sample access pattern, so the
// Pi's cache behaviour is measured before the real DSP exists.
//
// This is a benchmark, not a test: it prints numbers and always exits 0.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kOutputs = 2;
constexpr std::size_t kOversample = 4;
constexpr std::size_t kSampleRate = 48000;

std::size_t gridP = 128;
std::size_t gridPot = 33;

std::size_t index(std::size_t a, std::size_t b, std::size_t c)
{
  return ((c * gridP + b) * gridP + a) * kOutputs;
}

} // namespace

int main(int argc, char** argv)
{
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--grid" && i + 1 < argc) gridP = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--pot-positions" && i + 1 < argc) gridPot = std::strtoul(argv[++i], nullptr, 10);
  }

  std::vector<float> table(gridP * gridP * gridPot * kOutputs);
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i] = static_cast<float>(i % 1000) * 0.001f;
  }

  // One second of audio at the oversampled rate.
  const std::size_t samples = kSampleRate * kOversample;
  float accumulator = 0.0f;
  float phase = 0.0f;
  std::size_t pot = 0;

  const auto start = std::chrono::steady_clock::now();
  for (std::size_t n = 0; n < samples; ++n) {
    // A signal-like trajectory through the grid: smooth, as real audio is.
    phase += 0.01f;
    const float p1 = 0.5f + 0.45f * std::sin(phase);
    const float p2 = 0.5f + 0.45f * std::sin(phase * 1.7f);
    if (n % (kSampleRate / 4) == 0) pot = (pot + 1) % (gridPot - 1);

    const float fa = p1 * static_cast<float>(gridP - 2);
    const float fb = p2 * static_cast<float>(gridP - 2);
    const auto ia = static_cast<std::size_t>(fa);
    const auto ib = static_cast<std::size_t>(fb);
    const float wa = fa - static_cast<float>(ia);
    const float wb = fb - static_cast<float>(ib);

    // Trilinear: 8 corners, 2 outputs each.
    for (std::size_t out = 0; out < kOutputs; ++out) {
      float corner = 0.0f;
      for (std::size_t dc = 0; dc < 2; ++dc) {
        const float c00 = table[index(ia, ib, pot + dc) + out];
        const float c10 = table[index(ia + 1, ib, pot + dc) + out];
        const float c01 = table[index(ia, ib + 1, pot + dc) + out];
        const float c11 = table[index(ia + 1, ib + 1, pot + dc) + out];
        const float top = c00 + wa * (c10 - c00);
        const float bottom = c01 + wa * (c11 - c01);
        corner += top + wb * (bottom - top);
      }
      accumulator += corner;
    }

    // Stand-in for the ~200 flops of state-space arithmetic per sample.
    for (int k = 0; k < 50; ++k) {
      accumulator = accumulator * 0.9999f + 0.0001f;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double seconds = std::chrono::duration<double>(elapsed).count();

  std::printf("grid: %zu x %zu x %zu\n", gridP, gridP, gridPot);
  std::printf("table bytes: %zu\n", table.size() * sizeof(float));
  std::printf("one audio second took: %.4f s\n", seconds);
  std::printf("core fraction: %.2f%%\n", seconds * 100.0);
  std::printf("checksum: %f\n", accumulator);
  return 0;
}
