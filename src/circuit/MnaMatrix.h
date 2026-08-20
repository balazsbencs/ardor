#pragma once

#include <cstddef>
#include <vector>

// Dense linear algebra and nodal stamping, shared by the circuit derivations.
//
// This is deliberately hand-rolled rather than pulled from a matrix library.
// Every caller solves a system of a few dozen unknowns, offline or at control
// rate, so a third-party dependency would be a new build input bought for
// nothing. It was extracted from the wah derivation when the second circuit
// needed the same pieces; nothing here knows about any particular circuit.
//
// Ground is represented by a negative node index and carries no equation, so
// every stamping function ignores negative indices.
namespace ardor::circuit {

struct Mat {
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<double> v;

  Mat() = default;
  Mat(std::size_t r, std::size_t c) : rows(r), cols(c), v(r * c, 0.0) {}
  double& at(std::size_t r, std::size_t c) { return v[r * cols + c]; }
  double at(std::size_t r, std::size_t c) const { return v[r * cols + c]; }
};

Mat multiply(const Mat& a, const Mat& b);

// Solves A X = B by LU with partial pivoting. A is taken by value and
// destroyed. Throws if the system is singular, which for a circuit means the
// netlist has a floating node or a shorted source.
Mat solve(Mat a, Mat b);

void stampResistor(Mat& s, int a, int b, double ohms);

// Voltage-controlled current source: gm * (v[cp] - v[cn]) flows out of node
// `from` and into node `to`.
void stampVccs(Mat& s, int from, int to, int cp, int cn, double gm);

// Reads a node out of a solution vector, treating ground as zero volts.
double nodeVoltage(const std::vector<double>& v, int node);

// Shockley current and its small-signal conductance, sharing one clamp on the
// exponent. Without the clamp the first Newton step from a cold start
// overflows to infinity and never recovers.
double junctionCurrent(double volts, double saturationCurrent, double thermalVolts);
double junctionConductance(double volts, double saturationCurrent, double thermalVolts);

} // namespace ardor::circuit
