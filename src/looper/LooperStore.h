#pragma once

#include "looper/LooperWav.h"
#include "preset/Preset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ardor {

struct LooperSourcePreset {
  int bank = 0;
  int slot = 0;
  std::string name;
};

struct LooperSaveRequest {
  std::string id; // empty creates a new loop set
  std::string name;
  LooperSourcePreset sourcePreset;
  Preset preset;
  LooperPausedSessionView session;
};

struct LooperLoadedTrack {
  bool present = false;
  LooperStereoAudio audio;
  float levelDb = 0.0f;
  float balance = 0.0f;
  bool muted = false;
};

struct LooperLoadedSet {
  std::string id;
  std::string name;
  std::string savedAt;
  uint32_t sampleRate = 0;
  uint64_t loopFrames = 0;
  LooperSourcePreset sourcePreset;
  Preset preset;
  std::array<LooperLoadedTrack, kLooperTrackCount> tracks {};

  // The spans remain valid until this loaded set or one of its audio vectors
  // is mutated. Suitable for RealtimeLooper::restorePausedSession().
  LooperPausedSessionView pausedSessionView() const noexcept;
};

struct LooperLibraryEntry {
  std::string id;
  std::string name;
  std::string savedAt;
  std::string sourcePresetName;
  uint64_t loopFrames = 0;
  std::size_t populatedTracks = 0;
  bool available = false;
  std::string unavailableReason;
};

class LooperStore {
public:
  using PresetValidator = std::function<bool(const Preset&, std::string&)>;

  explicit LooperStore(std::filesystem::path dataRoot);

  bool save(const LooperSaveRequest& request, std::string& savedId, std::string& error) const;
  bool load(std::string_view id, uint64_t maximumFrames,
            LooperLoadedSet& loopSet, std::string& error) const;
  std::vector<LooperLibraryEntry> list(
    uint64_t maximumFrames, const PresetValidator& validatePreset = {}) const;
  bool remove(std::string_view id, std::string& error) const;

  std::filesystem::path pathFor(std::string_view id) const;
  static bool isValidId(std::string_view id) noexcept;

private:
  std::filesystem::path root_;
};

} // namespace ardor
