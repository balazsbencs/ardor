#include "preset/RuntimeCommands.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace ardor {

std::vector<RuntimeCommand> consumeRuntimeCommands(const std::filesystem::path& dataRoot)
{
  namespace fs = std::filesystem;

  const fs::path directory = dataRoot / "runtime" / "commands";
  std::error_code ec;
  if (!fs::is_directory(directory, ec)) {
    return {};
  }

  std::vector<fs::path> paths;
  for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    std::error_code entryEc;
    if (entry.is_regular_file(entryEc) && entry.path().extension() == ".json") {
      paths.push_back(entry.path());
    }
    if (entryEc) {
      return {};
    }
  }
  if (ec) {
    return {};
  }
  std::sort(paths.begin(), paths.end());

  std::vector<RuntimeCommand> commands;
  for (const auto& path : paths) {
    try {
      std::ifstream input(path);
      nlohmann::json json;
      input >> json;
      const std::string type = json.value("type", "");
      if (type == "reload_assets") {
        commands.push_back({RuntimeCommandType::ReloadAssets});
      } else if (type == "apply_preset") {
        const int bank = json.value("bank", -1);
        const int slot = json.value("slot", -1);
        if (bank >= 0 && bank < 100 && slot >= 0 && slot < 4) {
          commands.push_back({RuntimeCommandType::ApplyPreset, bank, slot});
        }
      }
    } catch (const std::exception&) {
      // Bad commands are discarded so a malformed file cannot stall runtime
      // command processing indefinitely.
    }
    fs::remove(path, ec);
  }
  return commands;
}

} // namespace ardor
