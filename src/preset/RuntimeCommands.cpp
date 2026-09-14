#include "preset/RuntimeCommands.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace ardor {

namespace {

bool validApplyId(const std::string& id)
{
  constexpr std::string_view prefix = "apply-";
  if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  return std::all_of(id.begin() + static_cast<std::ptrdiff_t>(prefix.size()), id.end(),
                     [](char character) { return character >= '0' && character <= '9'; });
}

bool writeJsonAtomically(const std::filesystem::path& path, const nlohmann::json& value,
                         std::string& error)
{
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "could not create runtime state directory: " + ec.message();
    return false;
  }

  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not open runtime state temporary file: " + temporary.string();
      return false;
    }
    output << value.dump() << '\n';
    output.flush();
    if (!output) {
      error = "could not write runtime state temporary file: " + temporary.string();
      return false;
    }
  }
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    error = "could not publish runtime state: " + path.string();
    return false;
  }
  return true;
}

} // namespace

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
          commands.push_back({RuntimeCommandType::ApplyPreset,
                              json.value("id", std::string{}), bank, slot});
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

bool writeRuntimeApplyResult(const std::filesystem::path& dataRoot,
                             const std::string& id, const std::string& state,
                             int bank, int slot, const std::string& message,
                             std::string& error)
{
  if (!validApplyId(id)) {
    error = "invalid runtime apply id";
    return false;
  }
  const auto path = dataRoot / "runtime" / "apply-results" / (id + ".json");
  return writeJsonAtomically(path, nlohmann::json{
    {"id", id}, {"state", state}, {"bank", bank}, {"slot", slot},
    {"message", message},
  }, error);
}

bool writeRuntimeActivePreset(const std::filesystem::path& dataRoot,
                              int bank, int slot, const std::string& name,
                              std::string& error)
{
  const auto path = dataRoot / "runtime" / "active-preset.json";
  return writeJsonAtomically(path, nlohmann::json{
    {"bank", bank}, {"slot", slot}, {"name", name},
  }, error);
}

bool clearRuntimeActivePreset(const std::filesystem::path& dataRoot,
                              std::string& error)
{
  error.clear();
  std::error_code ec;
  std::filesystem::remove(dataRoot / "runtime" / "active-preset.json", ec);
  if (ec) {
    error = "could not clear active preset state: " + ec.message();
    return false;
  }
  return true;
}

} // namespace ardor
