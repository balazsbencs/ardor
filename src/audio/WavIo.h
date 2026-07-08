#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ardor {

struct MonoWav {
  std::vector<float> samples;
  uint32_t sampleRate = 0;
};

MonoWav readMonoWav(const std::filesystem::path& path);

} // namespace ardor
