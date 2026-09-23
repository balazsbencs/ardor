#include "preset/RuntimeCommands.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

int main()
{
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "ardor-runtime-command-smoke";
  fs::remove_all(root);
  const fs::path commands = root / "runtime" / "commands";
  fs::create_directories(commands);

  {
    std::ofstream out(commands / "command-0001.json");
    out << R"({"type":"reload_assets"})";
  }
  {
    std::ofstream out(commands / "command-0002.json");
    out << R"({"type":"apply_preset","id":"apply-123","bank":2,"slot":3,"sceneId":"solo","revision":"rev-1"})";
  }
  {
    std::ofstream out(commands / "command-invalid.json");
    out << "not json";
  }
  const fs::path liveCommands = root / "runtime" / "live-commands";
  fs::create_directories(liveCommands);
  {
    std::ofstream out(liveCommands / "command-0001.json");
    out << R"({"type":"recall_scene","generation":91,"sceneId":"solo","requestId":"browser-1"})";
  }

  const auto consumed = ardor::consumeRuntimeCommands(root);
  if (consumed.size() != 3 || consumed[0].type != ardor::RuntimeCommandType::ReloadAssets ||
      consumed[1].type != ardor::RuntimeCommandType::ApplyPreset || consumed[1].bank != 2 ||
      consumed[1].slot != 3 || consumed[1].id != "apply-123" || consumed[1].sceneId != "solo" ||
      consumed[1].revision != "rev-1" ||
      consumed[2].type != ardor::RuntimeCommandType::RecallScene ||
      consumed[2].generation != 91 || consumed[2].sceneId != "solo" ||
      consumed[2].requestId != "browser-1") {
    std::cerr << "runtime command parse failed\n";
    return 1;
  }
  if (fs::directory_iterator(commands) != fs::directory_iterator{}) {
    std::cerr << "commands were not consumed\n";
    return 1;
  }
  if (fs::directory_iterator(liveCommands) != fs::directory_iterator{}) {
    std::cerr << "live commands were not consumed\n";
    return 1;
  }

  std::string error;
  if (!ardor::writeRuntimeApplyResult(root, "apply-123", "applied", 2, 3, "", error)) {
    std::cerr << "runtime apply result write failed: " << error << '\n';
    return 1;
  }
  if (!ardor::writeRuntimeActivePreset(root, 2, 3, "Applied", error, 91, "solo", 2, "rev-1")) {
    std::cerr << "runtime active preset write failed: " << error << '\n';
    return 1;
  }
  {
    std::ifstream input(root / "runtime" / "active-preset.json");
    nlohmann::json state;
    input >> state;
    if (state.value("generation", 0) != 91 || state.value("liveSceneId", "") != "solo"
        || state.value("liveSceneIndex", -1) != 2 || state.value("revision", "") != "rev-1") {
      std::cerr << "runtime scene state was not published\n";
      return 1;
    }
  }
  if (!fs::exists(root / "runtime" / "apply-results" / "apply-123.json")
      || !fs::exists(root / "runtime" / "active-preset.json")) {
    std::cerr << "runtime state was not published\n";
    return 1;
  }
  if (!ardor::clearRuntimeActivePreset(root, error)
      || fs::exists(root / "runtime" / "active-preset.json")) {
    std::cerr << "runtime active preset state was not cleared\n";
    return 1;
  }
  fs::remove_all(root);
  return 0;
}
