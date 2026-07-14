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
// This is deliberately not an automatic gain normalizer: cabinet gain is part
// of a capture's sound and remains under explicit user/preset control.
bool prepareMonoIr(MonoWav& wav, size_t maximumSamples, std::string& error);

} // namespace ardor
