#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ardor {

struct MonoWav {
  std::vector<float> samples;
  uint32_t sampleRate = 0;
  uint32_t channels = 0;
};

MonoWav readMonoWav(const std::filesystem::path& path);

// Validates an IR for the live mono cabinet path and, when capped, applies a
// short fade to avoid turning a hard truncation into an audible discontinuity.
// It then uses a guarded -1 dB ceiling for the estimated frequency-response peak
// (max|H(f)|), so a hot cabinet cannot add 12-15 dB of resonance on top of the
// NAM model and exhaust the engine's shared output headroom. Attenuation only:
// quiet or near-silent captures are left unchanged (never boosted), so a
// malformed/noise-only IR cannot become a blast. A single scalar preserves each
// capture's frequency response and phase exactly apart from level. Note this
// bounds steady-state sinusoidal gain, not arbitrary transient sample peaks -
// the engine's final limiter still guards those.
bool prepareMonoIr(MonoWav& wav, size_t maximumSamples, std::string& error);

} // namespace ardor
