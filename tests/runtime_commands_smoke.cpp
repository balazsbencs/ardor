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
    out << R"({"type":"apply_preset","bank":2,"slot":3})";
  }
  {
    std::ofstream out(commands / "command-invalid.json");
    out << "not json";
  }

  const auto consumed = ardor::consumeRuntimeCommands(root);
  if (consumed.size() != 2 || consumed[0].type != ardor::RuntimeCommandType::ReloadAssets ||
      consumed[1].type != ardor::RuntimeCommandType::ApplyPreset || consumed[1].bank != 2 ||
      consumed[1].slot != 3) {
    std::cerr << "runtime command parse failed\n";
    return 1;
  }
  if (fs::directory_iterator(commands) != fs::directory_iterator{}) {
    std::cerr << "commands were not consumed\n";
    return 1;
  }
  fs::remove_all(root);
  return 0;
}
