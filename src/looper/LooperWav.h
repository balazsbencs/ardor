#pragma once

#include "looper/RealtimeLooper.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

struct LooperStereoAudio {
  uint32_t sampleRate = 0;
  std::vector<float> left;
  std::vector<float> right;
};

bool writeLooperFloatWav(const std::filesystem::path& path,
                         const LooperPausedTrackView& track,
                         uint64_t loopFrames,
                         uint32_t sampleRate,
                         std::string& error);

bool readLooperFloatWav(const std::filesystem::path& path,
                        uint64_t maximumFrames,
                        LooperStereoAudio& audio,
                        std::string& error);

} // namespace ardor
