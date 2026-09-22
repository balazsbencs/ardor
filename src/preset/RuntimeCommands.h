#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

enum class RuntimeCommandType {
  ReloadAssets,
  ApplyPreset,
  RecallScene,
};

struct RuntimeCommand {
  RuntimeCommandType type = RuntimeCommandType::ReloadAssets;
  std::string id;
  int bank = 0;
  int slot = 0;
  std::uint64_t generation = 0;
  std::string sceneId;
  std::string requestId;
  std::string revision;
};

// Commands are created with an atomic rename by managerd. This function runs
// only on the pedal management loop, never on the audio callback.
std::vector<RuntimeCommand> consumeRuntimeCommands(const std::filesystem::path& dataRoot);

// These functions run on the pedal management thread. They publish control
// plane state atomically so managerd never observes a partially-written JSON
// document.
bool writeRuntimeApplyResult(const std::filesystem::path& dataRoot,
                             const std::string& id, const std::string& state,
                             int bank, int slot, const std::string& message,
                             std::string& error);
bool writeRuntimeActivePreset(const std::filesystem::path& dataRoot,
                              int bank, int slot, const std::string& name,
                              std::string& error,
                              std::uint64_t generation = 0,
                              const std::string& liveSceneId = {},
                              int liveSceneIndex = -1,
                              const std::string& revision = {});
bool clearRuntimeActivePreset(const std::filesystem::path& dataRoot,
                              std::string& error);

} // namespace ardor
