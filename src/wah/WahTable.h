#pragma once

#include "wah/WahDk.h"
#include "wah/WahNetlist.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

// On-disk layout for the precomputed wah model. Little-endian; both the
// development host and the Pi are ARM LE, and the file is regenerated rather
// than ported, so no byte-swapping path exists.
struct WahTableHeader {
  std::uint32_t magic = 0x48415751;   // "QWAH"
  std::uint32_t version = 1;
  std::uint32_t gridP = 0;            // Samples per p axis.
  std::uint32_t gridPot = 0;          // Pot positions.
  std::uint32_t states = 0;
  std::uint32_t ports = 0;
  std::uint32_t inputs = 0;
  float sampleRate = 0.0f;            // The OVERSAMPLED rate.
  float p1Min = 0.0f;
  float p1Max = 0.0f;
  float p2Min = 0.0f;
  float p2Max = 0.0f;
};

// Per pot position the matrices are packed in this order, row-major:
// A, B, C, D, E, F, G, H, K. Sizes follow the header's states/ports/inputs.
std::size_t wahMatrixStride(const WahTableHeader& header);

struct WahTable {
  WahTableHeader header;
  std::vector<float> matrices;  // gridPot * wahMatrixStride
  std::vector<float> solutions; // gridPot * gridP * gridP * ports
};

// Solves v = p + F i(v) for the port currents by damped Newton-Raphson.
// Returns false if it fails to converge within `maxIterations`.
//
// Two guards are load-bearing and must not be removed: the exponent argument
// is clamped, or a cold start overflows to infinity and never recovers; and
// each step's voltage change is limited, or Newton on a diode exponential
// oscillates instead of converging.
bool solveWahPort(const WahDkMatrices& matrices, const WahNetlist& netlist,
                  double p1, double p2, double& i1, double& i2, int maxIterations);

// Runs the model at `position` over `frames` of `amplitude` sine input using a
// live Newton solve at every sample, and widens `p1Min`..`p2Max` to cover the
// excursion actually observed. This is how the grid bounds get chosen: guessing
// them would either clip the table or waste most of its resolution.
void measureWahPortRange(const WahNetlist& netlist, double sampleRate, double position,
                         double amplitude, int frames,
                         double& p1Min, double& p1Max, double& p2Min, double& p2Max);

// Derives matrices at `potCount` evenly spaced positions and Newton-solves the
// nonlinear system at every grid point. Slow by design — this is the offline
// path. Bounds are measured internally unless all four are finite and ordered.
bool buildWahTable(const WahNetlist& netlist, double sampleRate,
                   std::uint32_t gridP, std::uint32_t potCount,
                   WahTable& out, std::string& error);

bool writeWahTable(const std::filesystem::path& path, const WahTable& table, std::string& error);
bool readWahTable(const std::filesystem::path& path, WahTable& table, std::string& error);

} // namespace ardor
