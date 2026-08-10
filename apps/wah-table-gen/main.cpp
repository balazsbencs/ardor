// Offline generator for the wah nonlinear solution table.
//
// The output is checked into assets/ and CI verifies regeneration is
// byte-identical, so this tool never runs during a cross-compile.
#include "wah/WahNetlist.h"
#include "wah/WahTable.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
  std::filesystem::path output = "assets/wah/gcb95.wahtable";
  double sampleRate = 192000.0;
  std::uint32_t gridP = 128;
  std::uint32_t potCount = 33;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", arg.c_str());
        std::exit(1);
      }
      return argv[++i];
    };
    if (arg == "--output") output = value();
    else if (arg == "--sample-rate") sampleRate = std::stod(value());
    else if (arg == "--grid") gridP = static_cast<std::uint32_t>(std::stoul(value()));
    else if (arg == "--pot-positions") potCount = static_cast<std::uint32_t>(std::stoul(value()));
    else {
      std::fprintf(stderr, "usage: wah-table-gen [--output PATH] [--sample-rate HZ]"
                           " [--grid N] [--pot-positions N]\n");
      return 1;
    }
  }

  ardor::WahTable table;
  std::string error;
  if (!ardor::buildWahTable(ardor::gcb95Netlist(), sampleRate, gridP, potCount, table, error)) {
    std::fprintf(stderr, "failed to build table: %s\n", error.c_str());
    return 1;
  }
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path());
  }
  if (!ardor::writeWahTable(output, table, error)) {
    std::fprintf(stderr, "failed to write %s: %s\n", output.string().c_str(), error.c_str());
    return 1;
  }
  std::printf("wrote %s\n", output.string().c_str());
  std::printf("  grid %u x %u x %u, %zu solutions, %zu matrix floats\n",
              table.header.gridP, table.header.gridP, table.header.gridPot,
              table.solutions.size(), table.matrices.size());
  std::printf("  p1 %.6f..%.6f  p2 %.6f..%.6f\n",
              table.header.p1Min, table.header.p1Max,
              table.header.p2Min, table.header.p2Max);
  return 0;
}
