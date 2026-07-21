#pragma once

#include <filesystem>
#include <vector>

namespace ardor {

enum class RuntimeCommandType {
  ReloadAssets,
  ApplyPreset,
};

struct RuntimeCommand {
  RuntimeCommandType type = RuntimeCommandType::ReloadAssets;
  int bank = 0;
  int slot = 0;
};

// Commands are created with an atomic rename by managerd. This function runs
// only on the pedal management loop, never on the audio callback.
std::vector<RuntimeCommand> consumeRuntimeCommands(const std::filesystem::path& dataRoot);

} // namespace ardor
