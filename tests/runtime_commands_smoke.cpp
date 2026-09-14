#include "preset/RuntimeCommands.h"

#include <filesystem>
#include <fstream>
#include <iostream>

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
    out << R"({"type":"apply_preset","id":"apply-123","bank":2,"slot":3})";
  }
  {
    std::ofstream out(commands / "command-invalid.json");
    out << "not json";
  }

  const auto consumed = ardor::consumeRuntimeCommands(root);
  if (consumed.size() != 2 || consumed[0].type != ardor::RuntimeCommandType::ReloadAssets ||
      consumed[1].type != ardor::RuntimeCommandType::ApplyPreset || consumed[1].bank != 2 ||
      consumed[1].slot != 3 || consumed[1].id != "apply-123") {
    std::cerr << "runtime command parse failed\n";
    return 1;
  }
  if (fs::directory_iterator(commands) != fs::directory_iterator{}) {
    std::cerr << "commands were not consumed\n";
    return 1;
  }

  std::string error;
  if (!ardor::writeRuntimeApplyResult(root, "apply-123", "applied", 2, 3, "", error)) {
    std::cerr << "runtime apply result write failed: " << error << '\n';
    return 1;
  }
  if (!ardor::writeRuntimeActivePreset(root, 2, 3, "Applied", error)) {
    std::cerr << "runtime active preset write failed: " << error << '\n';
    return 1;
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
